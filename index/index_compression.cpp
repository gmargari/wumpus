/**
 * Copyright (C) 2007 Stefan Buettcher. All rights reserved.
 * This is free software with ABSOLUTELY NO WARRANTY.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA
 **/

/**
 * Implementation of a couple of different index compression algorithms:
 *
 * - Gamma
 * - Interpolative
 * - Rice
 * - vByte
 * - GUBC
 * - GUBC-IP (aka GUBC-n)
 * - LLRUN
 * - ...
 *
 * Compressed posting lists always have the same header in this framework
 * (regardless of the actual compression algorithm used to produce the list).
 * The first byte tells us the type of the compression algorithm used. The
 * following N bytes contain the length of the list (number of postings),
 * encoded using vByte. The header is followed by the compressed postings.
 *
 * author: Stefan Buettcher
 * created: 2004-11-11
 * changed: 2010-04-04
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "index_compression.h"
#include "index_types.h"
#include "../feedback/dmc.h"
#include "../misc/all.h"


using namespace std;


#define READ_ONE_BIT(bitarray, position) \
	((bitarray[(position) >> 3] & (1 << ((position) & 7))) != 0 ? 1 : 0)

#define WRITE_ONE_BIT(value, bitarray, position) { \
	if ((value) != 0) \
		bitarray[(position) >> 3] |= (1 << ((position) & 7)); \
	else \
		bitarray[(position) >> 3] &= (0xFF ^ (1 << ((position) & 7))); \
}

#define READ_N_BITS(result, n, bitarray, position) { \
	result = 0; \
	for (int chaosCounter = n - 1; chaosCounter >= 0; chaosCounter--) \
		result = (result << 1) + READ_ONE_BIT(bitarray, (position) + chaosCounter); \
}

#define WRITE_N_BITS(value, n, bitarray, position) { \
	offset tempValue = value; \
	for (int chaosCounter = 0; chaosCounter < n; chaosCounter++) { \
		WRITE_ONE_BIT(tempValue & 1, bitarray, (position) + chaosCounter) \
		tempValue = tempValue >> 1; \
	} \
}

#define FLUSH_BIT_BUFFER() { \
	while (bitsInBuffer >= 8) { \
		result[bytePtr++] = (bitBuffer & 255); \
		bitBuffer >>= 8; \
		bitsInBuffer -= 8; \
	} \
} \

/**
 * This is used to switch between high-effectiveness compression methods
 * like GUBC-3 and Huffman and high-efficiency methods like vByte. For
 * smaller lists, it is not worthwhile to use the better methods, as no
 * disk I/O will be saved (due to blocked index access). This is the
 * heuristic threshold used to select between Huffman/GUBC-3 and vByte.
 **/
#define FANCY_COMPRESSION_THRESHOLD 32

/**
 * Whether we want to add padding bytes at the end of an encoded
 * postings list so that we can over-read in the decoder without
 * risking a bus error or segmentation fault.
 **/
#define PAD_ENCODED_LIST_FOR_OVERREADING false

/**
 * This array helps us determine the length of a gamma code word, based on
 * the gamma preamble. It is used in decompressGamma_FAST and allows us to
 * search for the first zero bit in the bit buffer.
 **/
static byte whereIsFirstZeroBit[256];
static byte whereIsFirstOneBit[256];
static bool lookAheadInitialized_GAMMA = false;

static void initializeLookAhead_GAMMA() {
	for (int i = 0; i < sizeof(whereIsFirstZeroBit); i++) {
		for (int k = 0; k < 10; k++)
			if (!(i & (1 << k))) {
				whereIsFirstZeroBit[i] = k + 1;
				break;
			}
		for (int k = 0; k < 10; k++)
			if (i & (1 << k)) {
				whereIsFirstOneBit[i] = k + 1;
				break;
			}
	}
	whereIsFirstOneBit[0] = 9;
	lookAheadInitialized_GAMMA = true;
}

/**
 * This array helps us decoding GroupVarInt-encoded posting lists.
 * See Jeff Dean's WSDM talk for details.
 **/

struct GroupVarIntHelper {
	int offset1, offset2, offset3, offset4;
	uint32_t mask1, mask2, mask3, mask4;
};
static GroupVarIntHelper groupVarIntLookupTable[256];
static bool groupVarInt_Initialized = false;

static void getGroupVarIntMask(int selector, int *numBytes, uint32_t *mask) {
	*numBytes = (selector & 3) + 1;
	*mask = (1LL << (*numBytes * 8)) - 1;
}

static void initializeGroupVarInt() {
	for (int selector = 0; selector < 256; selector++) {
		GroupVarIntHelper *helper = &groupVarIntLookupTable[selector];
		getGroupVarIntMask((selector >> 0) & 3, &helper->offset1, &helper->mask1);
		helper->offset1 += 1;
		getGroupVarIntMask((selector >> 2) & 3, &helper->offset2, &helper->mask2);
		helper->offset2 += helper->offset1;
		getGroupVarIntMask((selector >> 4) & 3, &helper->offset3, &helper->mask3);
		helper->offset3 += helper->offset2;
		getGroupVarIntMask((selector >> 6) & 3, &helper->offset4, &helper->mask4);
		helper->offset4 += helper->offset3;
	}
	groupVarInt_Initialized = true;
}


static inline int getBitCnt(offset n) {
	int result = 1;
	while ((n >> result) > 0)
		result += 2;
	if ((n >> (result - 1)) == 0)
		result--;
	return (result ? result : 1);
} // end of getBitCnt(offset)


static inline int getBitCnt(int b, offset n) {
	if (b <= 1)
		return 0;
	else {
		offset representable = (ONE << (b - 1));
		int result = b;
		while (n >= representable) {
			n >>= (b - 1);
			result += b;
		}
		return result;
	}	
} // end of getBitCnt(int, offset)


int encodeFrontCoding(const char *plain, const char *reference, byte *compressed) {
	int result = 0;
	int len = strlen(plain);

	// determine length of prefix and suffix
	int prefixLen = 0;
	while ((prefixLen < len) && (plain[prefixLen] == reference[prefixLen]))
		prefixLen++;
	int suffixLen = len - prefixLen;

	// encode length of prefix and suffix
	int p = prefixLen, s = suffixLen;
	while ((p >= 15) || (s >= 15)) {
		compressed[result++] = MIN(15, p) + (MIN(15, s) << 4);
		p -= MIN(15, p);
		s -= MIN(15, s);
	}
	compressed[result++] = p + (s << 4);

	// copy suffix
	memcpy(&compressed[result], &plain[prefixLen], suffixLen);
	return result + suffixLen;
} // end of encodeFrontCoding(char*, char*, char*)


int decodeFrontCoding(const byte *compressed, const char *reference, char *plain) {
	// extract length of prefix and suffix
	int result = 0, prefixLen = 0, suffixLen = 0;
	do {
		prefixLen += (compressed[result] & 15);
		suffixLen += (compressed[result] >> 4);
		result++;
	} while (((compressed[result - 1] & 15) == 15) || ((compressed[result - 1] >> 4) == 15));

	// copy prefix from "reference" and suffix from "compressed"
	memcpy(plain, reference, prefixLen);
	memcpy(&plain[prefixLen], &compressed[result], suffixLen);
	plain[prefixLen + suffixLen] = 0;
	return result + suffixLen;
} // end of decodeFrontCoding(char*, char*, char*)


/**
 * Reads the compression header, makes sure the compression mode is correct,
 * and returns a pointer to the output buffer, allocating memory if necessary.
 **/
static offset *readHeader(byte *compressedData, int compressionMode,
		int *listLength, int *bytePtr, offset *outputBuffer) {
	assert((compressedData[0] & 127) == compressionMode);
	*bytePtr = 1 + decodeVByte32(listLength, &compressedData[1]);
	if (outputBuffer == NULL)
		outputBuffer = typed_malloc(offset, (*listLength) + 1);
	return outputBuffer;
} // end of readHeader(byte*, int, int*, int*)


void sortHuffmanStructsByID(HuffmanStruct *array, int length) {
	for (int i = 0; i < length; i++) {
		while (array[i].id != i) {
			HuffmanStruct temp = array[i];
			array[i] = array[temp.id];
			array[temp.id] = temp;
		}
	}
} // end of sortHuffmanStructsByID(HuffmanStruct*, int)


static int compareHuffmanStructsByLength(const void *a, const void *b) {
	return ((HuffmanStruct*)a)->codeLength - ((HuffmanStruct*)b)->codeLength;
} // end of compareHuffmanStructsByLength(const void*, const void*)


static void sortHuffmanStructsByLength(HuffmanStruct *array, int length) {
	qsort(array, length, sizeof(HuffmanStruct), compareHuffmanStructsByLength);
} // end of sortHuffmanStructsByLength(HuffmanStruct*, int)


static inline int reverseBits(int n, int bitCnt) {
	int result = 0;
	for (int i = 0; i < bitCnt; i++) {
		result = (result << 1) | (n & 1);
		n >>= 1;
	}
	return result;
}


void computeHuffmanCodesFromCodeLengths(HuffmanStruct *array, int length) {
	sortHuffmanStructsByLength(array, length);
	int currentCodeLength = 0;
	int currentCodeWord = 0;
	for (int i = 0; i < length; i++) {
		if (array[i].codeLength == 0)
			continue;
		if (array[i].codeLength != currentCodeLength) {
			currentCodeWord <<= (array[i].codeLength - currentCodeLength);
			currentCodeLength = array[i].codeLength;
		}
		array[i].code = reverseBits(currentCodeWord, currentCodeLength);
		currentCodeWord++;
	}
} // end of computeHuffmanCodesFromCodeLengths(HuffmanStruct*, int)


void doHuffman(HuffmanStruct *array, int length) {
	assert(length >= 1);
	assert(length <= 64);
	int nodes[128], activeNodes[64], frequencies[128], leftChild[128], rightChild[128];
	int nodeCnt = 0;
	int activeNodeCnt = 0;

	// initialize node table
	for (int i = 0; i < length; i++) {
		if (array[i].frequency > 0) {
			nodes[nodeCnt] = i;
			frequencies[nodeCnt] = array[i].frequency;
			activeNodes[activeNodeCnt++] = nodeCnt++;
		}
		else
			array[i].codeLength = array[i].code = 0;
		array[i].id = i;
	}

	// build Huffman tree
	while (activeNodeCnt > 1) {
		int smallest = 0, secondSmallest = 1;
		if (frequencies[activeNodes[1]] < frequencies[activeNodes[0]]) {
			smallest = 1;
			secondSmallest = 0;
		}
		for (int i = 2; i < activeNodeCnt; i++) {
			if (frequencies[activeNodes[i]] < frequencies[activeNodes[smallest]]) {
				secondSmallest = smallest;
				smallest = i;
			}
			else if (frequencies[activeNodes[i]] < frequencies[activeNodes[secondSmallest]])
				secondSmallest = i;
		}
		frequencies[nodeCnt] =
			frequencies[activeNodes[smallest]] + frequencies[activeNodes[secondSmallest]];
		leftChild[nodeCnt] = activeNodes[smallest];
		rightChild[nodeCnt] = activeNodes[secondSmallest];
		if (smallest > secondSmallest)
			activeNodes[smallest] = activeNodes[--activeNodeCnt];
		activeNodes[secondSmallest] = activeNodes[--activeNodeCnt];
		if (smallest < secondSmallest)
			activeNodes[smallest] = activeNodes[--activeNodeCnt];
		nodes[nodeCnt] = -1;
		activeNodes[activeNodeCnt++] = nodeCnt++;
	}

	// compute code lengths, from back to front
	frequencies[nodeCnt - 1] = 0;
	for (int i = nodeCnt - 1; i >= 0; i--) {
		if (nodes[i] >= 0)
			array[nodes[i]].codeLength = frequencies[i];
		else
			frequencies[leftChild[i]] = frequencies[rightChild[i]] = frequencies[i] + 1;
	}

	// compute codes
	computeHuffmanCodesFromCodeLengths(array, length);

	// sort array and return
	sortHuffmanStructsByID(array, length);
} // end of doHuffman(HuffmanStruct*, int)


static void computeHuffmanMapping(char *mapping, int size, HuffmanStruct *array, int len) {
	memset(mapping, -1, size);
	for (int i = 0; i < len; i++)
		if ((array[i].codeLength > 0) && ((1 << array[i].codeLength) <= size)) {
			int hc = array[i].code;
			int increment = (1 << array[i].codeLength);
			for (int k = hc; k < size; k += increment) {
				assert(mapping[k] == -1);
				mapping[k] = i;
			}
		}
} // end of computeHuffmanMapping(byte*, int, HuffmanStruct*, int)


void restrictHuffmanCodeLengths(HuffmanStruct *array, int length, int maxCodeLen) {
	assert(length < (1 << maxCodeLen));
	int violators[64];
	int violatorCount = 0;
	for (int i = 0; i < length; i++)
		if (array[i].codeLength > maxCodeLen)
			violators[violatorCount++] = i;
	if (violatorCount == 0)
		return;
	int bestCandidate = -1;
	int spaceAtBest = 999;
	for (int i = 0; i < length; i++)
		if (array[i].codeLength < maxCodeLen) {
			int spaceHere = (1 << (maxCodeLen - array[i].codeLength - 1));
			if ((spaceHere >= violatorCount) && (spaceHere < spaceAtBest)) {
				bestCandidate = i;
				spaceAtBest = spaceHere;
			}
		}
	assert(bestCandidate >= 0);
	array[bestCandidate].codeLength++;
	for (int i = 0; (i < spaceAtBest) && (i < violatorCount); i++)
		array[violators[i]].codeLength = maxCodeLen;
	restrictHuffmanCodeLengths(array, length, maxCodeLen);
} // end of restrictHuffmanCodeLengths(HuffmanStruct*, int, int)


byte * compressHuffmanDirect(offset *uncompressed, int listLen, int *byteLen) {
	HuffmanStruct huffman[32];
	memset(huffman, 0, sizeof(huffman));

	// collect frequencies and initialize huffman table
	int bitCount = 1;
	for (int i = 0; i < listLen; i++) {
		offset delta = uncompressed[i];
		assert(delta > 0);

		if (delta <= 3)
			huffman[delta - 1].frequency++;
		else {
			while (delta >= (TWO << bitCount))
				bitCount++;
			while (delta < (ONE << bitCount))
				bitCount--;
			assert(bitCount >= 2);
			assert(bitCount < 31);
			huffman[bitCount + 1].frequency++;
		}
	}

	// compute Huffman codes for prefices
	int nonZeroCount = 0;
	for (int i = 0; i < 32; i++) {
		huffman[i].id = i;
		if (huffman[i].frequency > 0)
			nonZeroCount++;
	}

	// build Huffman tree
	doHuffman(huffman, 32);

	// make sure that no Huffman code is longer than 10 bits
	restrictHuffmanCodeLengths(huffman, 32, 10);
	computeHuffmanCodesFromCodeLengths(huffman, 32);
	sortHuffmanStructsByID(huffman, 32);

	// allocate memory and write compression header to output buffer
	byte *result = (byte*)malloc(listLen * 8 + 256);
	result[0] = COMPRESSION_HUFFMAN_DIRECT;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);

	// initialize bit buffer
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	// write huffman preamble to output buffer
	int lastNonZero = 0;
	for (int i = 0; i < 32; i++)
		if (huffman[i].frequency > 0)
			lastNonZero = i;
		else
			huffman[i].codeLength = 0;
	if (nonZeroCount == 1)
		huffman[lastNonZero].codeLength = 1;
	for (int i = 0; i < 32; i++) {
		bitBuffer |= (huffman[i].codeLength << bitsInBuffer);
		bitsInBuffer += 4;
		if (i == lastNonZero) {
			bitBuffer |= (15 << bitsInBuffer);
			bitsInBuffer += 4;
			break;
		}
		if (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}

	// encode sequence
	bitCount = 1;
	for (int i = 0; i < listLen; i++) {
		offset who = uncompressed[i];
		if (who <= 3) {
			// if it's a small gap, encode directly
			bitBuffer += (huffman[who - 1].code) << bitsInBuffer;
			bitsInBuffer += huffman[who - 1].codeLength;
		}
		else {
			// if it's a larger gap, encode in two parts
			while (who >= (TWO << bitCount))
				bitCount++;
			while (who < (ONE << bitCount))
				bitCount--;
			bitBuffer += (huffman[bitCount + 1].code) << bitsInBuffer;
			bitsInBuffer += huffman[bitCount + 1].codeLength;
			bitBuffer |= ((who ^ (ONE << bitCount)) << bitsInBuffer);
			bitsInBuffer += bitCount;
		}
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}

	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;
	result = (byte*)realloc(result, bytePtr);
	*byteLen = bytePtr;
	return result;
} // end of compressHuffmanDirect(offset*, int, int*)


offset * decompressHuffmanDirect(byte *compressed, int byteLen, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_HUFFMAN_DIRECT, &listLen, &bytePtr, outBuf);
	*listLength = listLen;
	offset *uncompressed = result;

	offset bitBuffer = compressed[bytePtr++];
	int bitsInBuffer = 8;

	// read huffman preamble: number of huffman code bits for each code word
	HuffmanStruct huffman[32];
	memset(huffman, 0, sizeof(huffman));
	int nonZeroCount = 0;
	for (int i = 0; i < 32; i++)
		huffman[i].id = i;
	for (int i = 0; (bitBuffer & 15) != 15; i++) {
		huffman[i].codeLength = (bitBuffer & 15);
		if (huffman[i].codeLength > 0)
			nonZeroCount++;
		bitsInBuffer -= 4;
		bitBuffer >>= 4;
		if (bitsInBuffer < 8) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}
	}
	bitsInBuffer -= 4;
	bitBuffer >>= 4;

	// build huffman tree and construct selector lookahead table
	char huffmanMapping[1024];
	if (nonZeroCount == 1) {
		for (int i = 0; i < 32; i++)
			if (huffman[i].codeLength != 0) {
				for (int k = 0; k < sizeof(huffmanMapping); k++)
					huffmanMapping[k] = i;
				huffman[i].codeLength = 0;
				break;
			}
	}
	else {
		computeHuffmanCodesFromCodeLengths(huffman, 32);
		sortHuffmanStructsByID(huffman, 32);
		computeHuffmanMapping(huffmanMapping, sizeof(huffmanMapping), huffman, 32);
	}

	for (int i = 0; i < listLen; i++) {
		while ((bytePtr < byteLen) && (bitsInBuffer < 56)) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}
		int who = huffmanMapping[bitBuffer & (sizeof(huffmanMapping) - 1)];
		int hcl = huffman[who].codeLength;
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;
		if (who <= 2)
			*uncompressed++ = who + 1;
		else {
			who--;
			offset mask = (ONE << who);
			*uncompressed++ = (bitBuffer & (mask - 1)) | mask;
			bitBuffer >>= who;
			bitsInBuffer -= who;
		}
	}

	return result;
} // end of decompressHuffmanDirect(byte*, int, int*, offset*)


byte * compressLLRunWithGivenModel(
		offset *uncompressed, int listLen, HuffmanStruct *model, int *byteLen) {
	// allocate memory and write compression header to output buffer
	byte *result = (byte*)malloc(listLen * 8 + 32);
	result[0] = COMPRESSION_INVALID;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	// initialize bit buffer
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	// encode postings
	int bitCount = 1;
	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1];

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;

		bitBuffer |= (model[bitCount].code << bitsInBuffer);
		bitsInBuffer += model[bitCount].codeLength;
		bitBuffer |= ((delta ^ (ONE << bitCount)) << bitsInBuffer);
		bitsInBuffer += bitCount;

		// flush bit buffer if appropriate
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	result = (byte*)realloc(result, bytePtr);
	*byteLen = bytePtr;
	return result;
} // end of compressLLRunWithGivenModel(offset*, int, HuffmanStruct*, int*)


offset * decompressLLRunWithGivenModel(
		byte *compressed, int byteLen, HuffmanStruct *model, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_LLRUN, &listLen, &bytePtr, outBuf);
	*listLength = listLen;

	offset *uncompressed = result;
	bytePtr += decodeVByteOffset(uncompressed++, &compressed[bytePtr]);

	offset bitBuffer = compressed[bytePtr++];
	int bitsInBuffer = 8;

	// construct selector lookahead table
	char huffmanMapping[1024];
	computeHuffmanMapping(huffmanMapping, sizeof(huffmanMapping), model, 40);

	// decompress first posting
	int hcl;
	offset previous = result[0];

	// initialize bit buffer and align memory access
	while (bytePtr & 3) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

	// decompress remaining postings
	int separator = 0;
	for (int i = separator; i < listLen; i++) {
		while ((bytePtr < byteLen) && (bitsInBuffer < 56)) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}
		int who = huffmanMapping[bitBuffer & (sizeof(huffmanMapping) - 1)];
		offset mask = (ONE << who);
		hcl = model[who].codeLength;
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;
		offset delta = (bitBuffer & (mask - 1)) | mask;
		bitBuffer >>= who;
		bitsInBuffer -= who;
		*uncompressed++ = previous = previous + delta;
	}

	return result;
} // end of decompressLLRunWithGivenModel(byte*, int, HuffmanStruct*, int*, offset*)


byte * compressLLRun(offset *uncompressed, int listLen, int *byteLen) {
	if (listLen < FANCY_COMPRESSION_THRESHOLD)
		return compressVByte(uncompressed, listLen, byteLen);

	// collect frequencies of d-gaps and initialize huffman table
	HuffmanStruct huffman[32];
	memset(huffman, 0, sizeof(huffman));
	int bitCount = 1;
	offset previous = -1;

	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1];
		assert(delta > 0);

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;
		if (bitCount >= 32)
			return compressGUBCIP(uncompressed, listLen, byteLen);
		huffman[bitCount].frequency++;
	}

	// compute Huffman codes for prefices
	int nonZeroCount = 0, lastNonZero = 0;
	for (int i = 0; i < 32; i++) {
		huffman[i].id = i;
		if (huffman[i].frequency > 0) {
			lastNonZero = i;
			nonZeroCount++;
		}
	}

	// allocate memory and write compression header to output buffer
	byte *result = (byte*)malloc(listLen * 8 + 256);
	result[0] = COMPRESSION_LLRUN;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	// initialize bit buffer
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	if (nonZeroCount == 1) {
		huffman[lastNonZero].code = huffman[lastNonZero].codeLength = 0;
		bitBuffer |= ((15 + (lastNonZero << 4)) << bitsInBuffer);
		bitsInBuffer += 10;
	}
	else {
		doHuffman(huffman, 32);

		// make sure that no Huffman code is longer than 10 bits
		restrictHuffmanCodeLengths(huffman, 32, 10);
		computeHuffmanCodesFromCodeLengths(huffman, 32);
		sortHuffmanStructsByID(huffman, 32);

		// write huffman preamble to output buffer
		for (int i = 0; i < 32; i++)
			if (huffman[i].frequency == 0)
				huffman[i].codeLength = 0;
		for (int i = 0; i < 32; i++) {
			bitBuffer |= (huffman[i].codeLength << bitsInBuffer);
			bitsInBuffer += 4;
			if (i == lastNonZero) {
				bitBuffer |= (15 << bitsInBuffer);
				bitsInBuffer += 4;
				break;
			}
			if (bitsInBuffer >= 8) {
				result[bytePtr++] = bitBuffer;
				bitBuffer >>= 8;
				bitsInBuffer -= 8;
			}
		}
	} // end else [nonZeroCount > 1]


	// encode postings
	bitCount = 1;
	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1];

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;

		bitBuffer |= (huffman[bitCount].code << bitsInBuffer);
		bitsInBuffer += huffman[bitCount].codeLength;
		bitBuffer |= ((delta ^ (ONE << bitCount)) << bitsInBuffer);
		bitsInBuffer += bitCount;

		// flush bit buffer if appropriate
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	if (PAD_ENCODED_LIST_FOR_OVERREADING)
		bytePtr += 7; // so we can over-read in the decoder

	result = (byte*)realloc(result, bytePtr);
	*byteLen = bytePtr;
	return result;
} // end of compressLLRun(offset*, int, int*)


offset * decompressLLRun(byte *compressed, int byteLen, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_LLRUN, &listLen, &bytePtr, outBuf);
	*listLength = listLen;

	offset *uncompressed = result;
	bytePtr += decodeVByteOffset(uncompressed++, &compressed[bytePtr]);

	offset bitBuffer = compressed[bytePtr++];
	int bitsInBuffer = 8;

	// read huffman preamble: number of huffman code bits for each code word
	HuffmanStruct huffman[32];
	memset(huffman, 0, sizeof(huffman));
	for (int i = 0; i < 32; i++)
		huffman[i].id = i;

	char huffmanMapping[1024];
	if ((bitBuffer & 15) == 15) {
		// if the bit buffer starts with a sequence of 4 "1" bits, then the Huffman
		// tree only consists of a single element, stored in the following 6 bits
		bitsInBuffer -= 4;
		bitBuffer >>= 4;
		if (bitsInBuffer < 8) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// prepare lookup table for decoding
		int whichBucket = (bitBuffer & 63);
		bitsInBuffer -= 6;
		bitBuffer >>= 6;
		memset(huffmanMapping, whichBucket, sizeof(huffmanMapping));
		huffman[whichBucket].code = huffman[whichBucket].codeLength = 0;
	}
	else {
		// otherwise, all is normal
		for (int i = 0; (bitBuffer & 15) != 15; i++) {
			huffman[i].codeLength = (bitBuffer & 15);
			bitsInBuffer -= 4;
			bitBuffer >>= 4;
			if (bitsInBuffer < 8) {
				offset chunk = compressed[bytePtr++];
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 8;
			}
		}
		bitsInBuffer -= 4;
		bitBuffer >>= 4;

		// build huffman tree and construct selector lookahead table
		computeHuffmanCodesFromCodeLengths(huffman, 32);
		sortHuffmanStructsByID(huffman, 32);
		computeHuffmanMapping(huffmanMapping, sizeof(huffmanMapping), huffman, 32);
	}

	// decompress first posting
	int hcl;
	offset previous = result[0];

	// initialize bit buffer and align memory access
	while (bytePtr & 3) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

	// decompress remaining postings
#if __BYTE_ORDER == __LITTLE_ENDIAN
	assert(listLen > 30);
	int separator = listLen - 30;
	for (int i = 1; i < separator; i++) {
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}
		int who = huffmanMapping[bitBuffer & (sizeof(huffmanMapping) - 1)];
		offset mask = (ONE << who);
		hcl = huffman[who].codeLength;
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;
		offset delta = (bitBuffer & (mask - 1)) | mask;
		bitBuffer >>= who;
		bitsInBuffer -= who;
		*uncompressed++ = previous = previous + delta;
	}
#else
	int separator = 0;
#endif
	for (int i = separator; i < listLen; i++) {
		while ((bytePtr < byteLen) && (bitsInBuffer < 56)) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}
		int who = huffmanMapping[bitBuffer & (sizeof(huffmanMapping) - 1)];
		offset mask = (ONE << who);
		hcl = huffman[who].codeLength;
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;
		offset delta = (bitBuffer & (mask - 1)) | mask;
		bitBuffer >>= who;
		bitsInBuffer -= who;
		*uncompressed++ = previous = previous + delta;
	}

	return result;
} // end of decompressLLRun(byte*, int, int*, offset*)


byte *compressHuffman2(offset *uncompressed, int listLen, int *byteLen) {
	if (listLen < 256)
		return compressVByte(uncompressed, listLen, byteLen);

	// collect frequencies of d-gaps and initialize huffman table
	HuffmanStruct huffmanDOCID[32], huffmanTF[DOC_LEVEL_MAX_TF + 1];
	memset(huffmanDOCID, 0, sizeof(huffmanDOCID));
	memset(huffmanTF, 0, sizeof(huffmanTF));
	int bitCount = 1;
	offset previous = -1;

	previous = -1;
	huffmanDOCID[0].frequency = 1;
	huffmanTF[0].frequency = 1;
	for (int i = 0; i < listLen; i++) {
		offset current = uncompressed[i];
		offset delta = (current >> DOC_LEVEL_SHIFT) - previous;
		if (delta <= 0)
			return compressLLRun(uncompressed, listLen, byteLen);
		int tf = (current & DOC_LEVEL_MAX_TF);
		previous = (current >> DOC_LEVEL_SHIFT);

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;
		if (bitCount > 30)
			return compressGUBCIP(uncompressed, listLen, byteLen);
		huffmanDOCID[bitCount].frequency++;
		huffmanTF[tf].frequency++;
	}

	// compute Huffman codes
	int nonZeroCountDOCID = 0;
	for (int i = 0; i < 32; i++) {
		huffmanDOCID[i].id = i;
		if (huffmanDOCID[i].frequency > 0)
			nonZeroCountDOCID++;
	}
	int nonZeroCountTF = 0;
	for (int i = 0; i <= DOC_LEVEL_MAX_TF; i++) {
		huffmanTF[i].id = i;
		if (huffmanTF[i].frequency > 0)
			nonZeroCountTF++;
	}
	if ((nonZeroCountDOCID <= 1) || (nonZeroCountTF <= 1))
		return compressVByte(uncompressed, listLen, byteLen);

	// build huffman tree for DOCID and TF
	doHuffman(huffmanDOCID, 32);
	doHuffman(huffmanTF, DOC_LEVEL_MAX_TF + 1);

	// make sure that no huffman code is longer than 10 bits
	restrictHuffmanCodeLengths(huffmanDOCID, 32, 9);
	computeHuffmanCodesFromCodeLengths(huffmanDOCID, 32);
	sortHuffmanStructsByID(huffmanDOCID, 32);
	restrictHuffmanCodeLengths(huffmanTF, DOC_LEVEL_MAX_TF + 1, 9);
	computeHuffmanCodesFromCodeLengths(huffmanTF, DOC_LEVEL_MAX_TF + 1);
	sortHuffmanStructsByID(huffmanTF, DOC_LEVEL_MAX_TF + 1);

	// allocate memory and write compression header to output buffer
	byte *result = (byte*)malloc(listLen * 8 + 1024);
	result[0] = COMPRESSION_HUFFMAN2;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	// write huffman preamble to output buffer
	int huffmanCodeDOCID[32], huffmanCodeLengthDOCID[32];
	int huffmanCodeTF[DOC_LEVEL_MAX_TF + 1], huffmanCodeLengthTF[DOC_LEVEL_MAX_TF + 1];
	for (int i = 0; i < 32; i++) {
		huffmanCodeDOCID[i] = huffmanDOCID[i].code;
		huffmanCodeLengthDOCID[i] = huffmanDOCID[i].codeLength;
		if (huffmanDOCID[i].frequency > 0)
			bitBuffer |= (huffmanCodeLengthDOCID[i] << bitsInBuffer);
		bitsInBuffer += 4;
		if (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	for (int i = 0; i <= DOC_LEVEL_MAX_TF; i++) {
		huffmanCodeTF[i] = huffmanTF[i].code;
		huffmanCodeLengthTF[i] = huffmanTF[i].codeLength;
		if (huffmanTF[i].frequency > 0)
			bitBuffer |= (huffmanCodeLengthTF[i] << bitsInBuffer);
		bitsInBuffer += 4;
		if (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}

	// encode postings
	bitCount = 1;
	previous = -1;
	for (int i = 0; i < listLen; i++) {
		offset current = uncompressed[i];
		offset delta = (current >> DOC_LEVEL_SHIFT) - previous;
		int tf = (current & DOC_LEVEL_MAX_TF);
		previous = (current >> DOC_LEVEL_SHIFT);

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;

		// encode DOCID gap
		bitBuffer |= (huffmanCodeDOCID[bitCount] << bitsInBuffer);
		bitsInBuffer += huffmanCodeLengthDOCID[bitCount];
		bitBuffer |= ((delta ^ (ONE << bitCount)) << bitsInBuffer);
		bitsInBuffer += bitCount;

		// encode TF value
		bitBuffer |= (((offset)huffmanCodeTF[tf]) << bitsInBuffer);
		bitsInBuffer += huffmanCodeLengthTF[tf];

		// flush bit buffer if appropriate
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	result = (byte*)realloc(result, bytePtr);
	*byteLen = bytePtr;
	return result;
} // end of compressHuffman2(offset*, int, int*)


offset * decompressHuffman2(byte *compressed, int byteLen, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_HUFFMAN2, &listLen, &bytePtr, outBuf);
	*listLength = listLen;
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	// read huffman preamble for DOCID and TF components
	HuffmanStruct huffmanDOCID[32], huffmanTF[DOC_LEVEL_MAX_TF + 1];
	int codeLengthDOCID[32], codeLengthTF[DOC_LEVEL_MAX_TF + 1];
	for (int i = 0; i < 32; i++) {
		if (bitsInBuffer < 8) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}
		huffmanDOCID[i].id = i;
		codeLengthDOCID[i] = huffmanDOCID[i].codeLength = (bitBuffer & 15);
		bitBuffer >>= 4;
		bitsInBuffer -= 4;
	}
	for (int i = 0; i <= DOC_LEVEL_MAX_TF; i++) {
		if (bitsInBuffer < 8) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}
		huffmanTF[i].id = i;
		codeLengthTF[i] = huffmanTF[i].codeLength = (bitBuffer & 15);
		bitBuffer >>= 4;
		bitsInBuffer -= 4;
	}
	if (bytePtr & 1) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

	computeHuffmanCodesFromCodeLengths(huffmanDOCID, 32);
	sortHuffmanStructsByID(huffmanDOCID, 32);
	char huffmanMappingDOCID[512];
	computeHuffmanMapping(huffmanMappingDOCID, sizeof(huffmanMappingDOCID), huffmanDOCID, 32);

	computeHuffmanCodesFromCodeLengths(huffmanTF, DOC_LEVEL_MAX_TF + 1);
	sortHuffmanStructsByID(huffmanTF, DOC_LEVEL_MAX_TF + 1);
	char huffmanMappingTF[512];
	computeHuffmanMapping(huffmanMappingTF, sizeof(huffmanMappingTF), huffmanTF, DOC_LEVEL_MAX_TF + 1);

	// decompress postings
	int hcl, who, tf;
	offset previous = -1;
	offset *uncompressed = result;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	assert(listLen >= 32);
	int separator = listLen - 32;
	for (int i = 0; i < separator; i++) {
		// load data into the bit buffer
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

		who = huffmanMappingDOCID[bitBuffer & (sizeof(huffmanMappingDOCID) - 1)];
		offset mask = (ONE << who);
		hcl = codeLengthDOCID[who];
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;
		offset delta = (bitBuffer & (mask - 1)) | mask;
		bitBuffer >>= who;
		bitsInBuffer -= who;
		previous += delta;

		tf = huffmanMappingTF[bitBuffer & (sizeof(huffmanMappingTF) - 1)];
		hcl = codeLengthTF[tf];
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;

		*uncompressed++ = (previous << DOC_LEVEL_SHIFT) | tf;
	}
#else
	int separator = 0;
#endif
	for (int i = separator; i < listLen; i++) {
		// load data into the bit buffer
		while ((bytePtr < byteLen) && (bitsInBuffer < 56)) {
			offset chunk = compressed[bytePtr++];
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 8;
		}

		who = huffmanMappingDOCID[bitBuffer & (sizeof(huffmanMappingDOCID) - 1)];
		offset mask = (ONE << who);
		hcl = codeLengthDOCID[who];
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;
		offset delta = (bitBuffer & (mask - 1)) | mask;
		bitBuffer >>= who;
		bitsInBuffer -= who;
		previous += delta;

		tf = huffmanMappingTF[bitBuffer & (sizeof(huffmanMappingTF) - 1)];
		hcl = codeLengthTF[tf];
		bitBuffer >>= hcl;
		bitsInBuffer -= hcl;

		*uncompressed++ = (previous << DOC_LEVEL_SHIFT) | tf;
	}

	return result;
} // end of decompressHuffman2(byte*, int, int*, offset*)


byte * compressLLRunMulti(offset *uncompressed, int listLen, int *byteLen) {
#define SECOND_ORDER 0
	static const int PARTITIONS = 4;

	if (listLen < 128 * (PARTITIONS - 1))
		return compressLLRun(uncompressed, listLen, byteLen);


	// collect frequencies of d-gaps and initialize huffman table
	HuffmanStruct huffman[PARTITIONS][32];
	memset(huffman, 0, sizeof(huffman));
	int bitCount = 1;

	// collect global frequency information used for partitioning code words
	int frequencies[32];
	int subFrequencies[32][32];
	memset(frequencies, 0, sizeof(frequencies));
	memset(subFrequencies, 0, sizeof(subFrequencies));
	offset previous = uncompressed[0];
	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - previous;
		assert(delta > 0);
		int prevBitCount = bitCount;
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;
		if (bitCount >= 32)
			return compressGUBCIP(uncompressed, listLen, byteLen);
		frequencies[bitCount]++;
		subFrequencies[prevBitCount][bitCount]++;
		previous = uncompressed[i];
	}

	double N = listLen - 1;
	int bestSplit = -1;
	double bestKLD = 0;
	for (int i = 1; i < 32; i++) {
		double kld1 = 0, kld2 = 0;
		double count = 0;
		for (int k = 0; k < 32; k++) {
			subFrequencies[i][k] += subFrequencies[i - 1][k];
			count += subFrequencies[i][k];
		}
		if (count < 1)
			continue;
		if (count > listLen - 2)
			break;
		for (int k = 0; k < 32; k++) {
			double p = (subFrequencies[i][k] + 1) / count;
			double q = (frequencies[k] + 1) / 1.0 / listLen;
			double r = (frequencies[k] - subFrequencies[i][k] + 1) / (listLen - count);
			kld1 += p * log(p / q);
			kld2 += r * log(r / q);
		}
		double kld = kld1 * count + kld2 * (N - count);
		if (kld > bestKLD) {
			bestKLD = kld;
			bestSplit = i;
		}
	}

	// split everything into partitions
	int whichPartition[32];
	int accumulator, currentPartition = 0;
	if ((PARTITIONS == 2) || ((PARTITIONS == 4) && (SECOND_ORDER))) {
		int inFirst = 0, inSecond = 0;
		for (int i = 0; i < 32; i++) {
			if (i <= bestSplit) {
				inFirst += frequencies[i];
				whichPartition[i] = 0;
			}
			else {
				inSecond += frequencies[i];
				whichPartition[i] = 1;
			}
		}
#if 0
		accumulator = 0;
		for (int i = 0; i <= bestSplit; i++) {
			accumulator += frequencies[i];
			if (accumulator <= inFirst / 2)
				whichPartition[i] = 0;
			else
				whichPartition[i] = 1;
		}
		accumulator = 0;
		for (int i = bestSplit + 1; i < 32; i++) {
			accumulator += frequencies[i];
			if (accumulator <= inSecond / 2)
				whichPartition[i] = 2;
			else
				whichPartition[i] = 3;
		}
#endif
	}
	else {
		accumulator = 0;
		for (int i = 0; i < 32; i++) {
			accumulator += frequencies[i];
			if (accumulator > ((currentPartition + 1) * listLen) / PARTITIONS)
				currentPartition++;
			whichPartition[i] = currentPartition;
		}
	}

	// collect frequency information for all PARTITIONS huffman trees
	previous = uncompressed[0];
	currentPartition = 0;
	bitCount = 1;
	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - previous;
		assert(delta > 0);

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;
		huffman[currentPartition][bitCount].frequency++;

#if SECOND_ORDER
		currentPartition = (currentPartition / 2) + 2 * whichPartition[bitCount];
#endif
		currentPartition = whichPartition[bitCount];
		previous = uncompressed[i];
	}

	// allocate memory and write compression header to output buffer
	byte *result = (byte*)malloc(listLen * 8 + 256);
	result[0] = COMPRESSION_LLRUN_MULTI;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	// initialize bit buffer
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	// compute Huffman codes for all partitions and write Huffman trees to
	// output buffer
	for (int p = 0; p < PARTITIONS; p++) {
		int nonZeroCount = 0;
		int lastNonZero = 0;
		for (int i = 0; i < 32; i++) {
			huffman[p][i].id = i;
			if (huffman[p][i].frequency > 0) {
				lastNonZero = i;
				nonZeroCount++;
			}
		}

		if (nonZeroCount == 1) {
			// if there is only a single bucket which non-zero occurrence count,
			// store its ID instead of the whole Huffman tree
			huffman[p][lastNonZero].code = huffman[p][lastNonZero].codeLength = 0;
			bitBuffer |= ((15 + (lastNonZero << 4)) << bitsInBuffer);
			bitsInBuffer += 10;
			if (bitsInBuffer >= 8) {
				result[bytePtr++] = bitBuffer;
				bitBuffer >>= 8;
				bitsInBuffer -= 8;
			}
		}
		else {
			// construct Huffman code
			doHuffman(huffman[p], 32);

			// make sure that no Huffman code is longer than 10 bits
			restrictHuffmanCodeLengths(huffman[p], 32, 10);
			computeHuffmanCodesFromCodeLengths(huffman[p], 32);
			sortHuffmanStructsByID(huffman[p], 32);

			// write Huffman preamble for current partition to output buffer
			for (int i = 0; i < 32; i++) {
				if (huffman[p][i].frequency <= 0)
					huffman[p][i].codeLength = 0;
				bitBuffer |= (huffman[p][i].codeLength << bitsInBuffer);
				bitsInBuffer += 4;
				if (i == lastNonZero) {
					bitBuffer |= (15 << bitsInBuffer);
					bitsInBuffer += 4;
					break;
				}
				if (bitsInBuffer >= 8) {
					result[bytePtr++] = bitBuffer;
					bitBuffer >>= 8;
					bitsInBuffer -= 8;
				}
			}
		}
	} // end for (int p = 0; p < PARTITIONS; p++)

	// encode postings
	currentPartition = 0;
	bitCount = 1;
	previous = uncompressed[0];
	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - previous;

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;

		bitBuffer |= (huffman[currentPartition][bitCount].code << bitsInBuffer);
		bitsInBuffer += huffman[currentPartition][bitCount].codeLength;
		bitBuffer |= ((delta ^ (ONE << bitCount)) << bitsInBuffer);
		bitsInBuffer += bitCount;

		// flush bit buffer if appropriate
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}

#if SECOND_ORDER
		currentPartition = (currentPartition / 2) + 2 * whichPartition[bitCount];
#endif
		currentPartition = whichPartition[bitCount];
		previous = uncompressed[i];
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	result = (byte*)realloc(result, bytePtr);
	*byteLen = bytePtr;
	return result;
} // end of compressLLRunMulti(offset*, int, int*)


#define NUM_EXPERIMENTAL_CHUNKS 3

byte * compressExperimental(offset *uncompressed, int listLen, int *byteLen) {
	if (listLen < 256)
		return compressGroupVarInt(uncompressed, listLen, byteLen);

	// Collect frequencies of d-gaps and initialize huffman table.
	HuffmanStruct huffman[32];
	memset(huffman, 0, sizeof(huffman));
	int bitCount = 1;
	offset previous = -1;

	int numWithNonZeroFreq = 0;
	for (int i = 1; i < listLen; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1];
		assert(delta > 0);

		// Try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap.
		while (delta >= (TWO << bitCount))
			bitCount++;
		while (delta < (ONE << bitCount))
			bitCount--;
		if (bitCount >= 32)
			return compressVByte(uncompressed, listLen, byteLen);
		if (huffman[bitCount].frequency++ == 0)
			++numWithNonZeroFreq;
	}
	if (numWithNonZeroFreq <= 1)
		return compressLLRun(uncompressed, listLen, byteLen);

	// Compute the Huffman code and limit all codewords to 8 bits.
	doHuffman(huffman, 32);
	restrictHuffmanCodeLengths(huffman, 32, 8);
	computeHuffmanCodesFromCodeLengths(huffman, 32);
	sortHuffmanStructsByID(huffman, 32);

	// Compute the lookup table so that we can pass it to the decoder.
	// Each element in the lookup table contains the identity of the symbol
	// (the bucket in LLRUN) in the lower 5 bits and the codeword length in
	// the upper 3 bits.
	byte lookupTable[256];
	memset(lookupTable, 255, sizeof(lookupTable));
	for (int i = 0; i < 32; i++) {
		const unsigned int codeLength = huffman[i].codeLength;
		assert(codeLength <= 8);
		if (codeLength > 0) {
			int increment = (1 << codeLength);
			for (int k = huffman[i].code; k < 256; k += increment) {
				assert(lookupTable[k] == 255);
				lookupTable[k] = i + ((codeLength - 1) << 5);
			}
		}
	}

	// Allocate memory and write compression header to output buffer.
	byte *result = (byte*)malloc(320);
	result[0] = COMPRESSION_EXPERIMENTAL;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);

	memcpy(&result[bytePtr], lookupTable, sizeof(lookupTable));
	bytePtr += sizeof(lookupTable);

	// Encode the first posting using vByte.
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	byte *compressedPostings = (byte*)malloc(listLen * 5 + 8);
	int compressedPostingsPtr = 0;

	// Initialize bit buffer.
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	// The bit-position at which the second chunk starts.
	int bitPositionOfNthChunk[8];

	// Encode postings
	bitCount = 1;
	for (int start = 1; start <= NUM_EXPERIMENTAL_CHUNKS; start++) {
		bitPositionOfNthChunk[start] = 8 * compressedPostingsPtr + bitsInBuffer;
		for (int i = start; i < listLen; i += NUM_EXPERIMENTAL_CHUNKS) {
			offset delta = uncompressed[i] - uncompressed[i - 1];

			// Try to determine how many bits we need, starting the search
			// from the number of bits we needed for the previous d-gap
			while (delta >= (TWO << bitCount))
				bitCount++;
			while (delta < (ONE << bitCount))
				bitCount--;

			bitBuffer |= (huffman[bitCount].code << bitsInBuffer);
			bitsInBuffer += huffman[bitCount].codeLength;
			bitBuffer |= ((delta ^ (ONE << bitCount)) << bitsInBuffer);
			bitsInBuffer += bitCount;

			// flush bit buffer if appropriate
			while (bitsInBuffer >= 8) {
				compressedPostings[compressedPostingsPtr++] = bitBuffer;
				bitBuffer >>= 8;
				bitsInBuffer -= 8;
			}
		}
	}
	if (bitsInBuffer > 0)
		compressedPostings[compressedPostingsPtr++] = bitBuffer;

	// Store the offset of the second chunk in the compressed list and
	// copy the encoded postings over.
	for (int i = 2; i <= NUM_EXPERIMENTAL_CHUNKS; i++)
		bytePtr += encodeVByte32(bitPositionOfNthChunk[i], &result[bytePtr]);
	result = (byte*)realloc(result, bytePtr + compressedPostingsPtr);
	memcpy(&result[bytePtr], compressedPostings, compressedPostingsPtr);
	free(compressedPostings);

	*byteLen = bytePtr + compressedPostingsPtr;
	return result;
} // end of compressExperimental(offset*, int, int*)


offset * decompressExperimental(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_EXPERIMENTAL, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;
	compressed += bytePtr;

	// Get a pointer that tells us for the next 8 bits what the corresponding
	// bucket is. Each entry in this table occupies, well, 8 bits.
	byte* lookupTable = compressed;
	compressed += 256;

	// Decode the first posting.
	offset current;
	compressed += decodeVByteOffset(&current, compressed);
	offset *uncompressed = result;
	*uncompressed++ = current;

	// Decode the start offset of the second chunk.
	int32_t bitPtr1 = 0;
	int32_t bitPtr2, bitPtr3;
	compressed += decodeVByte32(&bitPtr2, compressed);
	compressed += decodeVByte32(&bitPtr3, compressed);

	// Go.
  const offset *limit =
		result + 1 + ((listLen - 1) / NUM_EXPERIMENTAL_CHUNKS) * NUM_EXPERIMENTAL_CHUNKS;
	while (uncompressed != limit) {
		const uint64_t bitBuffer1 = *((uint64_t*)&compressed[bitPtr1 >> 3]) >> (bitPtr1 & 7);
		const uint64_t bitBuffer2 = *((uint64_t*)&compressed[bitPtr2 >> 3]) >> (bitPtr2 & 7);
		const uint64_t bitBuffer3 = *((uint64_t*)&compressed[bitPtr3 >> 3]) >> (bitPtr3 & 7);
		const byte tableEntry1 = lookupTable[bitBuffer1 & 255];
		const byte tableEntry2 = lookupTable[bitBuffer2 & 255];
		const byte tableEntry3 = lookupTable[bitBuffer3 & 255];
		const byte codeLength1 = (tableEntry1 >> 5) + 1;
		const byte codeLength2 = (tableEntry2 >> 5) + 1;
		const byte codeLength3 = (tableEntry3 >> 5) + 1;
		const byte bucket1 = (tableEntry1 & 31);
		const byte bucket2 = (tableEntry2 & 31);
		const byte bucket3 = (tableEntry3 & 31);
		const uint32_t mask1 = (1 << bucket1);
		const uint32_t mask2 = (1 << bucket2);
		const uint32_t mask3 = (1 << bucket3);
		const uint32_t delta1 = ((bitBuffer1 >> codeLength1) & (mask1 - 1)) | mask1;
		const uint32_t delta2 = ((bitBuffer2 >> codeLength2) & (mask2 - 1)) | mask2;
		const uint32_t delta3 = ((bitBuffer3 >> codeLength3) & (mask3 - 1)) | mask3;
		*uncompressed++ = current = current + delta1;
		bitPtr1 += codeLength1 + bucket1;
		*uncompressed++ = current = current + delta2;
		bitPtr2 += codeLength2 + bucket2;
		*uncompressed++ = current = current + delta3;
		bitPtr3 += codeLength3 + bucket3;
	}
	if (uncompressed != result + listLen) {
		const uint64_t bitBuffer1 = *((uint64_t*)&compressed[bitPtr1 >> 3]) >> (bitPtr1 & 7);
		const byte tableEntry1 = lookupTable[bitBuffer1 & 255];
		const byte codeLength1 = (tableEntry1 >> 5) + 1;
		const byte bucket1 = (tableEntry1 & 31);
		const uint64_t mask1 = (1 << bucket1);
		const uint64_t delta1 = ((bitBuffer1 >> codeLength1) & (mask1 - 1)) | mask1;
		*uncompressed++ = current = current + delta1;
	}
	if (uncompressed != result + listLen) {
		const uint64_t bitBuffer2 = *((uint64_t*)&compressed[bitPtr2 >> 3]) >> (bitPtr2 & 7);
		const byte tableEntry2 = lookupTable[bitBuffer2 & 255];
		const byte codeLength2 = (tableEntry2 >> 5) + 1;
		const byte bucket2 = (tableEntry2 & 31);
		const uint64_t mask2 = (1 << bucket2);
		const uint64_t delta2 = ((bitBuffer2 >> codeLength2) & (mask2 - 1)) | mask2;
		*uncompressed++ = current = current + delta2;
	}

	return result;
} // end of decompressExperimental(byte*, int, int*, offset*)


byte * compressBest(offset *uncompressed, int listLen, int *byteLen) {
	static Compressor compressors[5] = {
		compressGamma,
		compressInterpolative,
		compressVByte,
		compressLLRun,
		compressLLRunMulti
	};
	byte *result = NULL;
	for (int i = 0; i < 5; i++) {
		int tempSize;
		byte *temp = compressors[i](uncompressed, listLen, &tempSize);
		if ((result != NULL) && (tempSize >= *byteLen))
			free(temp);
		else {
			if (result != NULL)
				free(result);
			result = temp;
			*byteLen = tempSize;
		}
	}
	assert(result != NULL);
	return result;
} // end of compressBest(offset*, int, int*)


byte *compressGamma(offset *uncompressed, int listLength, int *byteLength) {
	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 8 + 32);
	result[0] = COMPRESSION_GAMMA;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset previousElement = -1;
	int bitCount = 1;
	for (int i = 0; i < listLength; i++) {
		// encode i-th element of list
		offset delta = uncompressed[i] - previousElement;

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (ONE << bitCount))
			bitCount++;
		while (delta < (ONE << (bitCount - 1)))
			bitCount--;

		// write selector value in unary, using "bitCount" bits
		bitBuffer |= ((ONE << (bitCount - 1)) << bitsInBuffer);
		bitsInBuffer += bitCount;
		FLUSH_BIT_BUFFER();

		// write delta value as (bitCount-1)-bit binary number
		offset mask = (ONE << (bitCount - 1)) - 1;
		bitBuffer |= (delta & mask) << bitsInBuffer;
		bitsInBuffer += bitCount - 1;
		FLUSH_BIT_BUFFER();

		// prepare "previousElement" for the next iteration of the loop
		previousElement = uncompressed[i];
	} // end for (int i = 0; i < listLength; i++)

	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressGamma(offset*, int, int*)


offset * decompressGamma(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	if (!lookAheadInitialized_GAMMA)
		initializeLookAhead_GAMMA();

	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_GAMMA, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	if (bytePtr & 1) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

	offset *uncompressed = result;
	offset previous = -1;
	int separator = 0;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	separator = listLen - 48;
	for (int i = 0; i < separator; i++) {
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

		// search for first zero bit in bit buffer, indicating length of current code word
		int temp, bitCount = 1;
		do {
			temp = whereIsFirstOneBit[bitBuffer & 255] - 1;
			bitCount += temp;
			bitBuffer >>= temp;
		} while (temp >= 8);
		bitBuffer >>= 1;
		bitsInBuffer -= bitCount;

		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

		// extract delta value from bit buffer and store in result array
		offset mask = (ONE << (bitCount - 1)) - 1;
		*uncompressed++ = previous = previous + (bitBuffer & mask) + mask + 1;
		bitBuffer >>= (bitCount - 1);
		bitsInBuffer -= (bitCount - 1);
	}
#endif
	for (int i = MAX(0, separator); i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// search for first zero bit in bit buffer, indicating length of current code word
		int temp, bitCount = 1;
		do {
			temp = whereIsFirstOneBit[bitBuffer & 255] - 1;
			bitCount += temp;
			bitBuffer >>= temp;
		} while (temp >= 8);
		bitBuffer >>= 1;
		bitsInBuffer -= bitCount;

		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// extract delta value from bit buffer and store in result array
		offset mask = (ONE << (bitCount - 1)) - 1;
		*uncompressed++ = previous = previous + (bitBuffer & mask) + mask + 1;
		bitBuffer >>= (bitCount - 1);
		bitsInBuffer -= (bitCount - 1);
	}

	return result;
} // end of decompressGamma(byte*, int, int*, offset*)


byte *compressDelta(offset *uncompressed, int listLength, int *byteLength) {
	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 8 + 32);
	result[0] = COMPRESSION_DELTA;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);

	int bitLen[60] = { 0 };
	for (int i = 1; i < 60; i++)
		bitLen[i] = bitLen[i / 2] + 1;

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset previousElement = -1;
	offset bitCount = 1;
	for (int i = 0; i < listLength; i++) {
		// encode i-th element of list
		offset delta = uncompressed[i] - previousElement;

		// try to find out how many bits we need, starting the search
		// from the number of bits we needed for the previous d-gap
		while (delta >= (ONE << bitCount))
			bitCount++;
		while (delta < (ONE << (bitCount - 1)))
			bitCount--;

		// write gamma code for "bitCount"
		assert(bitCount > 0);
		int bl = bitLen[bitCount];
		bitBuffer |= ((ONE << (bl - 1)) << bitsInBuffer);
		bitsInBuffer += bl;
		bitBuffer |= (bitCount & (ONE << (bl - 1)) - 1) << bitsInBuffer;
		bitsInBuffer += bl - 1;
		FLUSH_BIT_BUFFER();

		// write gap value as (bitCount-1)-bit binary number
		offset mask = (ONE << (bitCount - 1)) - 1;
		bitBuffer |= (delta & mask) << bitsInBuffer;
		bitsInBuffer += bitCount - 1;
		FLUSH_BIT_BUFFER();

		// prepare "previousElement" for the next iteration of the loop
		previousElement = uncompressed[i];
	} // end for (int i = 0; i < listLength; i++)

	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressDelta(offset*, int, int*)


offset *decompressDelta(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	if (!lookAheadInitialized_GAMMA)
		initializeLookAhead_GAMMA();

	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_DELTA, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	if (bytePtr & 1) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

	offset *uncompressed = result;
	offset previous = -1;
	int separator = 0;

#if __BYTE_ORDER == __LITTLE_ENDIAN
	separator = listLen - 48;
	for (int i = 0; i < separator; i++) {
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

		// search for first bit-count (stored in unary)
		int bitCount = whereIsFirstOneBit[bitBuffer & 255];
		bitBuffer >>= bitCount;
		bitsInBuffer -= bitCount;

		// extract second bit-count (stored in binary, omitting the leading "1" bit)
		offset mask = (ONE << (bitCount - 1)) - 1;
		int bitCount2 = (bitBuffer & mask) + mask + 1;
		bitBuffer >>= (bitCount - 1);
		bitsInBuffer -= (bitCount - 1);
		
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

		// extract delta value from bit buffer and store in result array
		mask = (ONE << (bitCount2 - 1)) - 1;
		previous += (bitBuffer & mask) + mask + 1;
		bitBuffer >>= (bitCount2 - 1);
		bitsInBuffer -= (bitCount2 - 1);
		*(uncompressed++) = previous;
	}
#endif
	for (int i = MAX(0, separator); i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// search for first bit-count (stored in unary)
		int bitCount = whereIsFirstOneBit[bitBuffer & 255];
		bitBuffer >>= bitCount;
		bitsInBuffer -= bitCount;

		// extract second bit-count (stored in binary, omitting the leading "1" bit)
		offset mask = (ONE << (bitCount - 1)) - 1;
		int bitCount2 = (bitBuffer & mask) + mask + 1;
		bitBuffer >>= (bitCount - 1);
		bitsInBuffer -= (bitCount - 1);

		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// extract delta value from bit buffer and store in result array
		mask = (ONE << (bitCount2 - 1)) - 1;
		previous += (bitBuffer & mask) + mask + 1;
		bitBuffer >>= (bitCount2 - 1);
		bitsInBuffer -= (bitCount2 - 1);
		*(uncompressed++) = previous;
	}

	return result;
} // end of decompressDelta(byte*, int, int*, offset)


byte * compressNibble(offset *uncompressed, int listLen, int *byteLen) {
	byte *result = (byte*)malloc(listLen * 8 + 32);
	result[0] = COMPRESSION_NIBBLE;
	int bytePtr = 1 + encodeVByte32(listLen, &result[1]);
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset previous = -1;
	for (int i = 0; i < listLen; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous = uncompressed[i];

		while (delta >= 8) {
			bitBuffer += ((8 + (delta & 7)) << bitsInBuffer);
			bitsInBuffer += 4;
			delta >>= 3;
			if (bitsInBuffer > 56) {
				result[bytePtr++] = bitBuffer;
				bitBuffer >>= 8;
				bitsInBuffer -= 8;
			}
		}
		bitBuffer += (delta << bitsInBuffer);
		bitsInBuffer += 4;

		// flush as many bytes to the output buffer as possible
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	*byteLen = bytePtr;
	result = (byte*)realloc(result, bytePtr);
	return result;
} // end of compressNibble(offset*, int, int*)


offset * decompressNibble(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	int listLen, bytePtr, bitPtr;
	offset *result =
		readHeader(compressed, COMPRESSION_NIBBLE, &listLen, &bytePtr, outBuf);
	*listLength = listLen;

	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	offset prev = -1;
	for (int i = 0; i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset dummy = compressed[bytePtr++];
			bitBuffer |= (dummy << bitsInBuffer);
			bitsInBuffer += 8;
		}
		int shift = 0;
		while (bitBuffer & 8) {
			prev += (bitBuffer & 7) << shift;
			shift += 3;
			bitBuffer >>= 4;
			bitsInBuffer -= 4;
			if ((bitsInBuffer < 8) && (bytePtr < byteLength)) {
				offset dummy = compressed[bytePtr++];
				bitBuffer |= (dummy << bitsInBuffer);
				bitsInBuffer += 8;
			}
		}
		prev += 1 + ((bitBuffer & 7) << shift);
		result[i] = prev;
		bitBuffer >>= 4;
		bitsInBuffer -= 4;
	}

	return result;
} // end of decompressNibble(byte*, int, int*, offset*)


static short SIMPLE_9_TABLE[9] = {
	28 +  1 * 256,
	14 +  2 * 256,
	9 +  3 * 256,
	7 +  4 * 256,
	5 +  5 * 256,
	4 +  7 * 256,
	3 +  9 * 256,
	2 + 14 * 256,
	1 + 28 * 256,
};


byte * compressSimple9(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < FANCY_COMPRESSION_THRESHOLD)
		return compressVByte(uncompressed, listLength, byteLength);

	// allocate memory and output compression header
	byte *result = (byte*)malloc(listLength * 4 + 32);
	result[0] = COMPRESSION_SIMPLE_9;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);

	// word-align everything (will make decoding faster)
	while (bytePtr & 3)
		bytePtr++;

	int inPos = 0;
	while (inPos < listLength) {
		int match = 0;
		offset max = uncompressed[inPos] - (inPos == 0 ? 0 : uncompressed[inPos - 1] + 1);
		int maxBitCnt = getBitCnt(max);
		if (maxBitCnt > 28) {
			free(result);
			return compressVByte(uncompressed, listLength, byteLength);
		}
		for (int i = 1; i < 9; i++) {
			int count = (SIMPLE_9_TABLE[i] >> 8), width = (SIMPLE_9_TABLE[i] & 255);
			if (inPos + count > listLength)
				break;
			for (int k = (SIMPLE_9_TABLE[i - 1] >> 8); k < count; k++) {
				offset delta = uncompressed[inPos + k] - (uncompressed[inPos + k - 1] + 1);
				if (delta > max) {
					max = delta;
					maxBitCnt = getBitCnt(delta);
				}
			}
			if (maxBitCnt > width)
				break;
			match = i;
		}
		uint32_t w = match;
		int count = (SIMPLE_9_TABLE[match] >> 8), width = (SIMPLE_9_TABLE[match] & 255);
		for (int i = inPos; i < inPos + count; i++) {
			offset delta = uncompressed[i] - (i == 0 ? 0 : uncompressed[i - 1] + 1);
			w |= (delta << (4 + (i - inPos) * width));
		}
		*((uint32_t*)&result[bytePtr]) = w;
		bytePtr += 4;
		inPos += count;
	}

	*byteLength = bytePtr;
	result = (byte*)realloc(result, bytePtr);
	return result;
} // end of compressSimple9(offset*, int, int*)


offset * decompressSimple9(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_SIMPLE_9, &listLen, &bytePtr, outBuf);
	while (bytePtr & 3)
		bytePtr++;
	*listLength = listLen;
	offset *limit = &result[listLen];

	uint32_t *input = (uint32_t*)&compressed[bytePtr];
	offset *output = result, lastOffset = -1;
	while (output != limit) {
		uint32_t w = *input++;
		short s = SIMPLE_9_TABLE[w & 15];
		int count = (s >> 8) + 1, width = (s & 255);
		w >>= 4;
		while (--count) {
			lastOffset += 1 + (w & ((1 << width) - 1));
			*output++ = lastOffset;
			w >>= width;
		}
	}

	return result;
} // end of decompressSimple9(byte*, int, int*, offset*)


byte * compressGUBC(offset *uncompressed, int listLength, int *byteLength) {
	int histogram[64];
	memset(histogram, 0, sizeof(histogram));
	offset previous = -1;
	for (int i = 0; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous = uncompressed[i];
		histogram[getBitCnt(delta)]++;
	}
	int maxBitLen = 1;
	long long totalBitsUsed[16];
	memset(totalBitsUsed, 0, sizeof(totalBitsUsed));
	long long codeLength[64][16];
	for (int bitLength = 1; bitLength < 64; bitLength++) {
		if (histogram[bitLength] == 0)
			continue;
		assert(bitLength < 48);
		maxBitLen = bitLength;
		for (int chunkSize = 1; chunkSize < 16; chunkSize++) {
			if (bitLength > 8 * (chunkSize - 1))
				codeLength[bitLength][chunkSize] = 999999999;
			else
				codeLength[bitLength][chunkSize] =
					(chunkSize + 1) * ((bitLength + chunkSize - 1) / chunkSize);
			totalBitsUsed[chunkSize] +=
				histogram[bitLength] * codeLength[bitLength][chunkSize];
		}
	}

	int optimalChunkSize = 7;
	int bitCnt = totalBitsUsed[7];
	int startSearch = 1;
	if (maxBitLen > 28)
		startSearch++;
	if (maxBitLen > 36)
		startSearch++;
	for (int chunkSize = startSearch; chunkSize < 16; chunkSize++)
		if (totalBitsUsed[chunkSize] < bitCnt) {
			bitCnt = totalBitsUsed[chunkSize];
			optimalChunkSize = chunkSize;
		}
	*byteLength = (bitCnt + 7) / 8 + 10;

	byte *result = typed_malloc(byte, *byteLength);
	result[0] = COMPRESSION_GUBC;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);
	result[bytePtr++] = optimalChunkSize;
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	previous = -1;
	for (int i = 0; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous = uncompressed[i];

		int chunkCnt = 1;
		int bitCnt = optimalChunkSize;
		while ((ONE << bitCnt) <= delta) {
			delta -= (ONE << bitCnt);
			bitCnt += optimalChunkSize;
			chunkCnt++;
		}
		bitBuffer |= ((1 << (chunkCnt - 1)) - 1) << bitsInBuffer;
		bitsInBuffer += chunkCnt;
		bitBuffer |= (delta << bitsInBuffer);
		bitsInBuffer += bitCnt;

		// flush as many bytes to the output buffer as possible
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	assert(bytePtr < *byteLength);
	*byteLength = bytePtr;
	return result;
} // end of compressGUBC(offset*, int, int*)


offset * decompressGUBC(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	if (!lookAheadInitialized_GAMMA)
		initializeLookAhead_GAMMA();

	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_GUBC, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;
	int chunkSize = compressed[bytePtr++];
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset startOffset[32];
	int totalBitCount[32];
	startOffset[1] = 0;
	totalBitCount[1] = chunkSize;
	for (int i = 2; i < 32; i++) {
		startOffset[i] = startOffset[i - 1] + (ONE << totalBitCount[i - 1]);
		totalBitCount[i] = totalBitCount[i - 1] + chunkSize;
	}

	offset previous = -1;
	int outPos = 0;
#if __BYTE_ORDER == __LITTLE_ENDIAN
	int listLenMinus32 = listLen - 32;
	while (outPos < listLenMinus32) {
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

		// search for first zero bit in bit buffer, indicating length of current code word
		int chunkCount = whereIsFirstZeroBit[bitBuffer & 255];
		bitBuffer >>= chunkCount;
		bitsInBuffer -= chunkCount;
		int bitCount = totalBitCount[chunkCount];
		offset delta = startOffset[chunkCount];
		delta += (bitBuffer & ((ONE << bitCount) - 1));
		bitBuffer >>= bitCount;
		bitsInBuffer -= bitCount;
		previous += delta + 1;
		result[outPos++] = previous;
	}
#endif
	while (outPos < listLen) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// search for first zero bit in bit buffer, indicating length of current code word
		int chunkCount = whereIsFirstZeroBit[bitBuffer & 255];
		bitBuffer >>= chunkCount;
		bitsInBuffer -= chunkCount;
		int bitCount = totalBitCount[chunkCount];
		offset delta = startOffset[chunkCount];
		delta += (bitBuffer & ((ONE << bitCount) - 1));
		bitBuffer >>= bitCount;
		bitsInBuffer -= bitCount;
		previous += delta + 1;
		result[outPos++] = previous;
	}

	return result;
} // end of decompressGUBC(byte*, int, int*, offset*)
	

byte * compressGUBCIP(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < FANCY_COMPRESSION_THRESHOLD)
		return compressVByte(uncompressed, listLength, byteLength);

	long histogram[64];
	memset(histogram, 0, sizeof(histogram));
	offset previous = uncompressed[0];
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous = uncompressed[i];
		histogram[getBitCnt(delta)]++;
	}

	// for each combination (rho, sigma, tau), compute the overall memory requirement
	long long totalBitsUsed[16][16][6];
	memset(totalBitsUsed, 0, sizeof(totalBitsUsed));
	for (int bl = 1; bl < 64; bl++)
		if (histogram[bl] > 0) {
			if (bl >= 42)
				return compressVByte(uncompressed, listLength, byteLength);
			for (int rho = 1; rho < 16; rho++)
				for (int sigma = 1; sigma < 16; sigma++)
					for (int tau = 1; tau < 6; tau++) {
						int used = rho;
						int chunkCnt = 1;
						if (bl > rho) {
							int myBL = bl - rho - sigma;
							used += sigma;
							chunkCnt++;
							while (myBL > 0) {
								myBL -= tau;
								used += tau;
								chunkCnt++;
							}
						}
						used += chunkCnt;
						if ((used >= 48) || (chunkCnt > 16))
							totalBitsUsed[rho][sigma][tau] += 128 * histogram[bl] + 2000000000;
						else
							totalBitsUsed[rho][sigma][tau] += used * histogram[bl];
					}
		}

	int optRho = 7, optSigma = 7, optTau = 3;
	long long optBitsUsed = totalBitsUsed[optRho][optSigma][optTau];
	for (int rho = 1; rho < 16; rho++)
		for (int sigma = 1; sigma < 16; sigma++)
			for (int tau = 2; tau < 6; tau++)
				if (totalBitsUsed[rho][sigma][tau] < optBitsUsed) {
					optBitsUsed = totalBitsUsed[rho][sigma][tau];
					optRho = rho;
					optSigma = sigma;
					optTau = tau;
				}

	if (optTau <= 3) {
		int os = optSigma;
		int ot = optTau;
		for (int sigma = MAX(1, os - 1); sigma <= MIN(15, os + 1); sigma++)
			for (int tau = MAX(2, ot - 1); tau <= ot + 1; tau++) {
				if (totalBitsUsed[optRho][sigma][tau] > 2000000000)
					continue;
				long long tbu = 0;
				previous = uncompressed[0];
				for (int i = 1; i < listLength; i++) {
					offset delta = uncompressed[i] - previous - 1;
					previous += delta + 1;

					int chunkCnt = 1;
					int bitCnt = optRho;
					if (delta >= (ONE << bitCnt)) {
						delta -= (ONE << bitCnt);
						bitCnt += sigma;
						chunkCnt++;
					}
					while (delta >= (ONE << bitCnt)) {
						delta -= (ONE << bitCnt);
						bitCnt += tau;
						chunkCnt++;
					}

					tbu += bitCnt + chunkCnt;
				}
				if (tbu < optBitsUsed) {
					optBitsUsed = tbu;
					optSigma = sigma;
					optTau = tau;
				}
			}
	}

	int bitCnt = optBitsUsed;
	*byteLength = (bitCnt + 7) / 8 + 32;

	byte *result = typed_malloc(byte, *byteLength);
	result[0] = COMPRESSION_GUBCIP;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);
	result[bytePtr++] = (optRho + (optSigma << 4));
	result[bytePtr++] = (optTau + (1 << 4));
	byte *fastFlag = &result[bytePtr - 1];
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);	

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	previous = uncompressed[0];
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous += delta + 1;

		int chunkCnt = 1;
		int bitCnt = optRho;
		if (delta >= (ONE << bitCnt)) {
			delta -= (ONE << bitCnt);
			bitCnt += optSigma;
			chunkCnt++;
		}
		while (delta >= (ONE << bitCnt)) {
			delta -= (ONE << bitCnt);
			bitCnt += optTau;
			chunkCnt++;
		}

		if (bitCnt > 24)
			*fastFlag &= 15;

		bitBuffer |= (ONE << (chunkCnt - 1)) << bitsInBuffer;
		bitsInBuffer += chunkCnt;
		bitBuffer |= (delta << bitsInBuffer);
		bitsInBuffer += bitCnt;

		// flush as many bytes to the output buffer as possible
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	assert(bytePtr < *byteLength);
	*byteLength = bytePtr;
	return result;
} // end of compressGUBCIP(offset*, int, int*)


offset * decompressGUBCIP(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	if (!lookAheadInitialized_GAMMA)
		initializeLookAhead_GAMMA();

	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_GUBCIP, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;

	int rhoSigma = compressed[bytePtr++];
	int tauFast = compressed[bytePtr++];
	int rho = (rhoSigma & 15);
	int sigma = (rhoSigma >> 4);
	int tau = (tauFast & 15);
	int fast = (tauFast >> 4);
	
	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset startOffset[16];
	int bitCount[16];
	startOffset[1] = 1;
	bitCount[1] = rho;
	startOffset[2] = 1 + (ONE << rho);
	bitCount[2] = rho + sigma;
	for (int i = 3; i < 16; i++) {
		startOffset[i] = startOffset[i - 1] + (ONE << bitCount[i - 1]);
		bitCount[i] = bitCount[i - 1] + tau;
	}

	offset *uncompressed = result;
	bytePtr += decodeVByteOffset(uncompressed++, &compressed[bytePtr]);
	offset previous = result[0];
	int separator = 1;
	while (bytePtr & 3) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

#if __BYTE_ORDER == __LITTLE_ENDIAN
	separator = MAX(1, listLen - 32);
	int i = 1;
	if (fast) {
		for ( ; i < separator; i++) {
			if (bitsInBuffer < 32) {
				offset chunk = *((uint32_t*)&compressed[bytePtr]);
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 32;
				bytePtr += 4;
			}

			// search for first zero bit in bit buffer, indicating length of current code word
			int chunkCnt = whereIsFirstOneBit[bitBuffer & 255];
			if (chunkCnt > 8)
				goto nonfast;
			bitBuffer >>= chunkCnt;
			bitsInBuffer -= chunkCnt;

			// fetch "chunkCount" chunks of length rho, sigma, tau, ... from the bit buffer
			int bitCnt = bitCount[chunkCnt];
			uint32_t delta = startOffset[chunkCnt] + (bitBuffer & ((1 << bitCnt) - 1));
			bitBuffer >>= bitCnt;
			bitsInBuffer -= bitCnt;

			*uncompressed++ = previous = previous + delta;
		}
	}
	else {
nonfast:
		for ( ; i < separator; i++) {
			while (bitsInBuffer < 48) {
				offset chunk = *((uint16_t*)&compressed[bytePtr]);
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 16;
				bytePtr += 2;
			}

			// search for first zero bit in bit buffer, indicating length of current code word
			int chunkCnt = whereIsFirstOneBit[bitBuffer & 255];
			if (chunkCnt > 8)
				chunkCnt = 8 + whereIsFirstOneBit[(bitBuffer >> 8) & 255];
			bitBuffer >>= chunkCnt;
			bitsInBuffer -= chunkCnt;

			// fetch "chunkCount" chunks of length rho, sigma, tau, ... from the bit buffer
			int bitCnt = bitCount[chunkCnt];
			offset delta = startOffset[chunkCnt] + (bitBuffer & ((ONE << bitCnt) - 1));
			bitBuffer >>= bitCnt;
			bitsInBuffer -= bitCnt;

			*uncompressed++ = previous = previous + delta;
		}
	}
#endif
	for (int i = separator; i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		// search for first zero bit in bit buffer, indicating length of current code word
		int chunkCnt = whereIsFirstOneBit[bitBuffer & 255];
		if (chunkCnt > 8)
			chunkCnt = 8 + whereIsFirstOneBit[(bitBuffer >> 8) & 255];
		bitBuffer >>= chunkCnt;
		bitsInBuffer -= chunkCnt;

		// fetch "chunkCount" chunks of length rho, sigma, tau, ... from the bit buffer
		int bitCnt = bitCount[chunkCnt];
		offset delta = startOffset[chunkCnt] + (bitBuffer & ((ONE << bitCnt) - 1));
		bitBuffer >>= bitCnt;
		bitsInBuffer -= bitCnt;

		*uncompressed++ = previous = previous + delta;
	}

	return result;
} // end of decompressGUBCIP(byte*, int, int*, offset*)
	

static byte * compressInterpolative(
		offset *uncompressed, int listLength, int *byteLength, int compressionMode) {
	if (listLength < 8)
		return compressVByte(uncompressed, listLength, byteLength);

	// allocated memory for compressed postings
	int bytesAllocated = listLength * 7 + 16;
	byte *result = (byte*)malloc(bytesAllocated);

	offset first = uncompressed[0];
	offset last = uncompressed[listLength - 1];

	// encode preamble and initialize stack for recursive encoding
	result[0] = compressionMode;
	int bytesUsed = 1 + encodeVByte32(listLength, &result[1]);
	bytesUsed += encodeVByteOffset(first, &result[bytesUsed]);
	bytesUsed += encodeVByteOffset(last - first, &result[bytesUsed]);

	int leftEnd[40], rightEnd[40], location[40], bitWidth[40];
	int stackPtr = 1;
	leftEnd[0] = 0;
	rightEnd[0] = listLength - 1;
	location[0] = listLength - 1;
	bitWidth[0] = 63;
	leftEnd[1] = 0;
	rightEnd[1] = listLength - 1;
	location[1] = (listLength - 1) >> 1;
	bitWidth[1] = getBitCnt(uncompressed[listLength - 1] - uncompressed[0]);

#if 1
	static const double FREQ_BASE = 1.25;
	offset median = (ONE << 32), mean = (ONE << 32);
	offset *gaps = typed_malloc(offset, listLength - 1);
	int gapFreq[40];
	memset(gapFreq, 0, sizeof(gapFreq));
	for (int i = 1; i < listLength; i++) {
		gaps[i - 1] = uncompressed[i] - uncompressed[i - 1];
		gapFreq[getBitCnt(gaps[i - 1])]++;
	}
	sortOffsetsAscending(gaps, listLength - 1);
	median = gaps[listLength / 2];
	mean = (uncompressed[listLength - 1] - uncompressed[0]) / (listLength - 1);
	free(gaps);
#endif

	// initialize output buffer
	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	int left = leftEnd[stackPtr];
	int right = rightEnd[stackPtr];
	int here = location[stackPtr];

	double adjustment = 0;

	while (stackPtr > 0) {
		// flush output buffer if possible
		while (bitsInBuffer >= 8) {
			result[bytesUsed++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}

		if (here > left) {
			// determine bitwidth, based on parent's bitwidth
			offset gap = uncompressed[right] - uncompressed[left] - right + left + 1;
			if (gap > 1) {
				int bw = bitWidth[stackPtr - 1];
				gap <<= 1;
				while ((gap >> bw) == 0)
					bw -= 2;
				if (gap >> (bw + 1) != 0)
					bw += 1;
				bitWidth[stackPtr] = bw;

				// encode current posting, using either bw bits or (bw - 1) bits;
				// try to allocate (bw - 1) bits for gaps around the center of the
				// allocated gap and bw bits close to the edges
				offset allocated = (ONE << bw);
				offset middle = (allocated >> 1);
				offset used = uncompressed[right] - uncompressed[left] - right + left + 1;
				offset oneBitLessRegion = allocated - used;
				offset actualGap = uncompressed[here] - uncompressed[left] - here + left;

				if ((compressionMode == COMPRESSION_INTERPOLATIVE) || (right > left + 4)) {
					offset leftLimit = (used >> 1) - (oneBitLessRegion >> 1);
					offset rightLimit = leftLimit + oneBitLessRegion;
					// canonical interpolative coding
					if ((actualGap < leftLimit) || (actualGap >= rightLimit)) {
						if (actualGap >= rightLimit)
							actualGap -= oneBitLessRegion;
						if (actualGap + oneBitLessRegion < middle)
							actualGap += oneBitLessRegion;
						else
							actualGap += (oneBitLessRegion << 1);
						bitBuffer |= (actualGap << bitsInBuffer);
						bitsInBuffer += bw;
					}
					else {
						bitBuffer |= ((actualGap - leftLimit) << bitsInBuffer);
						bitsInBuffer += bw - 1;
					}
				}
				else if (false) {
					offset space = oneBitLessRegion / 2;
					int bitDiff = 1;
					while (space > median * 2) {
						space /= 2;
						bitDiff++;
					}
					if ((actualGap < space) || (actualGap > used - space))
						bitsInBuffer += bw - bitDiff;
					else
						bitsInBuffer += bw;
				}
				else {
					// optimization for schema-independent index compression
					offset delta = actualGap;
					if (delta >= middle)
						delta = used - actualGap;
					offset space = oneBitLessRegion / 4;
					int diff = 0;
					while (space >= (here - left) * 2 * median) {
						space /= 2;
						diff++;
					}
					offset totalSpace = oneBitLessRegion;
					offset rangeForBitDeduction[8];
					memset(rangeForBitDeduction, 0, sizeof(rangeForBitDeduction));
					for (int i = 1; i < 8; i++) {
						rangeForBitDeduction[i] = space;
						totalSpace -= (space << (i + diff));
						space /= 4;
					}
					for (int i = 7; i >= 1; i--) {
						while (totalSpace >= (ONE << (i + diff))) {
							rangeForBitDeduction[i]++;
							totalSpace -= (ONE << (i + diff));
						}
						rangeForBitDeduction[i - 1] += rangeForBitDeduction[i];
					}
					assert(totalSpace >= 0);
					bitsInBuffer += bw;
					if (delta < rangeForBitDeduction[1])
						bitsInBuffer -= diff;
					for (int i = 1; i < 8; i++)
						if (delta < rangeForBitDeduction[i])
							bitsInBuffer--;
				}

				// put left-hand child onto the stack
				if (here > left + 1) {
					stackPtr++;
					leftEnd[stackPtr] = left;
					rightEnd[stackPtr] = right = here;
					location[stackPtr] = here = (left + right) >> 1;
					continue;
				}
				else if (here < right - 1) {
					stackPtr++;
					leftEnd[stackPtr] = left = here;
					rightEnd[stackPtr] = right;
					location[stackPtr] = here = (left + right) >> 1;
					continue;			
				}
			}
		}  // end if (here > left)

		// if we are the right-hand child of the current parent, remove us from
		// the stack and keep removing ancestors until we hit the first ancestor
		// that is a left-hand child of its parent
		while (here >= location[stackPtr - 1]) {
			if (--stackPtr <= 0)
				break;
			else {
				left = leftEnd[stackPtr];
				right = rightEnd[stackPtr];
				here = location[stackPtr];
			}
		}

		if (stackPtr > 0) {
			// we now know that we are the left-hand child of the current parent;
			// replace us with the parent's right-hand child
			leftEnd[stackPtr] = left = right;
			rightEnd[stackPtr] = right = rightEnd[stackPtr - 1];
			location[stackPtr] = here = (left + right) >> 1;
		}

	} // end while (stackPtr > 0)

	while (bitsInBuffer > 0) {
		result[bytesUsed++] = (bitBuffer & 255);
		bitBuffer >>= 8;
		bitsInBuffer -= 8;
	}
	bytesUsed += (int)(adjustment / 8) + 1;

	*byteLength = bytesUsed;
	result = (byte*)realloc(result, bytesUsed);
	return result;
} // end of compressInterpolative(offset*, int, int*)


static offset * decompressInterpolative(
		byte *compressed, int byteLength, int *listLength, offset *outputBuffer, int compressionMode) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, compressionMode, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;

	offset first, deltaToLast;
	bytePtr += decodeVByteOffset(&first, &compressed[bytePtr]);
	bytePtr += decodeVByteOffset(&deltaToLast, &compressed[bytePtr]);
	result[0] = first;
	result[listLen - 1] = first + deltaToLast;

	int leftEnd[40], rightEnd[40], location[40], bitWidth[40];
	int stackPtr = 1;
	leftEnd[0] = 0;
	rightEnd[0] = listLen - 1;
	location[0] = listLen - 1;
	bitWidth[0] = 63;
	leftEnd[1] = 0;
	rightEnd[1] = listLen - 1;
	location[1] = (listLen - 1) >> 1;
	bitWidth[1] = getBitCnt(result[listLen - 1] - result[0]);

	// initialize output buffer
	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	int left = leftEnd[stackPtr];
	int right = rightEnd[stackPtr];
	int here = location[stackPtr];

	while (stackPtr > 0) {

		if (here > left) {
			// determine bitwidth, based on parent's bitwidth
			offset gap = result[right] - result[left] - right + left + 1;
			if (gap <= 1) {
				for (int i = left + 1; i < right; i++)
					result[i] = result[i - 1] + 1;
			}
			else {
				int bw = bitWidth[stackPtr - 1];
				gap <<= 1;
				while ((gap >> bw) == 0)
					bw -= 2;
				if (gap >> (bw + 1) != 0)
					bw += 1;
				bitWidth[stackPtr] = bw;

				// refill buffer if necessary
				if (bitsInBuffer < bw) {
					while (bitsInBuffer < bw) {
						offset chunk = compressed[bytePtr++];
						bitBuffer |= (chunk << bitsInBuffer);
						bitsInBuffer += 8;
					}
				}

				// fetch current delta from buffer and put into result array
				// encode current posting, using either bw bits or (bw - 1) bits
				offset allocated = (ONE << bw);
				offset middle = (allocated >> 1);
				offset used = result[right] - result[left] - right + left + 1;
				offset oneBitLessRegion = allocated - used;
				offset leftLimit = (used >> 1) - (oneBitLessRegion >> 1);
				offset rightLimit = leftLimit + oneBitLessRegion;
				offset actualGap = (bitBuffer & ((ONE << (bw - 1)) - 1));

				if ((compressionMode == COMPRESSION_INTERPOLATIVE) || (right > left + 4)) {
					// canonical interpolative coding
					if (actualGap < oneBitLessRegion) {
						actualGap += leftLimit;
						bitBuffer >>= (bw - 1);
						bitsInBuffer -= (bw - 1);
					}
					else {
						actualGap = (bitBuffer & ((ONE << bw) - 1));
						if (actualGap < middle)
							actualGap -= oneBitLessRegion;
						else
							actualGap -= (oneBitLessRegion << 1);
						if (actualGap >= leftLimit)
							actualGap += oneBitLessRegion;
						bitBuffer >>= bw;
						bitsInBuffer -= bw;
					}
				}
				else {
					// optimization for schema-independent index compression
				}
				result[here] = result[left] + actualGap + here - left;

				// put left-hand child onto the stack
				if (here > left + 1) {
					stackPtr++;
					leftEnd[stackPtr] = left;
					rightEnd[stackPtr] = right = here;
					location[stackPtr] = here = (left + right) >> 1;
					continue;
				}
				else if (here < right - 1) {
					stackPtr++;
					leftEnd[stackPtr] = left = here;
					rightEnd[stackPtr] = right;
					location[stackPtr] = here = (left + right) >> 1;
					continue;			
				}
			}
		} // end if (here > left)

		// if we are the right-hand child of the current parent, remove us from
		// the stack and keep removing ancestors until we hit the first ancestor
		// that is a left-hand child of its parent
		while (here >= location[stackPtr - 1]) {
			if (--stackPtr <= 0)
				break;
			else {
				left = leftEnd[stackPtr];
				right = rightEnd[stackPtr];
				here = location[stackPtr];
			}
		}

		if (stackPtr > 0) {
			// we now know that we are the left-hand child of the current parent;
			// replace us with the parent's right-hand child
			leftEnd[stackPtr] = left = right;
			rightEnd[stackPtr] = right = rightEnd[stackPtr - 1];
			location[stackPtr] = here = (left + right) >> 1;
		}

	} // end while (stackPtr > 0)

	return result;
} // end of decompressInterpolative2(byte*, int, int*, offset*, int)


byte * compressInterpolative(offset *uncompressed, int listLength, int *byteLength) {
	return compressInterpolative(uncompressed, listLength, byteLength, COMPRESSION_INTERPOLATIVE);
} // end of compressInterpolative(offset*, int, int*)


byte *compressInterpolative_SI(offset *uncompressed, int listLength, int *byteLength) {
	return compressInterpolative(uncompressed, listLength, byteLength, COMPRESSION_INTERPOLATIVE_SI);
} // end of compressInterpolative_SI(offset*, int, int*)


offset * decompressInterpolative(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	return decompressInterpolative(compressed, byteLength, listLength, outputBuffer, COMPRESSION_INTERPOLATIVE);
} // end of decompressInterpolative(byte*, int, int*, offset*)


offset * decompressInterpolative_SI(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	return decompressInterpolative(compressed, byteLength, listLength, outputBuffer, COMPRESSION_INTERPOLATIVE_SI);
} // end of decompressInterpolative_SI(byte*, int, int*, offset*)


byte * compressNone(offset *uncompressed, int listLength, int *byteLength) {
	// allocate space and store compression mode and list length in the header
	int outputBufferSize = listLength * sizeof(int32_t) + 32;
	byte *result = (byte*)malloc(outputBufferSize);
	result[0] = COMPRESSION_NONE;
	int outputBufferPos = 1 + encodeVByte32(listLength, &result[1]);

	// align memory accesses
	while (outputBufferPos & 3)
		outputBufferPos++;

	offset previous = 0;
	static const uint32_t MAX_ENCODABLE = 0x7FFFFFFF;
	for (int i = 0; i < listLength; i++) {
		offset delta = uncompressed[i] - previous;
		if (delta <= MAX_ENCODABLE) {
			*((uint32_t*)&result[outputBufferPos]) = (uint32_t)delta;
			outputBufferPos += sizeof(uint32_t);
		}
		else {
			uint32_t first = (delta & MAX_ENCODABLE);
			uint32_t second = (delta >> 31);
			*((uint32_t*)&result[outputBufferPos]) = first | (1 << 31);
			outputBufferPos += sizeof(uint32_t);
			*((uint32_t*)&result[outputBufferPos]) = second;
			outputBufferPos += sizeof(uint32_t);
		}
		previous = uncompressed[i];
		if (outputBufferPos > outputBufferSize - 32) {
			outputBufferSize = (int)(outputBufferSize * 1.31);
			result = (byte*)realloc(result, outputBufferSize);
		}
	} // end for (int i = 0; i < listLength; i++)

	if (outputBufferPos < outputBufferSize * 0.95)
		result = (byte*)realloc(result, outputBufferPos);

	*byteLength = outputBufferPos;
	return result;
} // end of compressNone(offset*, int, int*)


offset * decompressNone(byte *compressed, int byteLength, int *listLength, offset *outputBuffer) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_NONE, &listLen, &bytePtr, outputBuffer);
	*listLength = listLen;
	static const uint32_t MAX_ENCODABLE = 0x7FFFFFFF;

	// align memory accesses
	while (bytePtr & 3)
		bytePtr++;
	uint32_t *array = (uint32_t*)&compressed[bytePtr];

	offset previous = 0;
	for (int i = 0; i < listLen; i++) {
		offset delta = *array++;
		if (delta <= MAX_ENCODABLE)
			previous += delta;
		else {
			offset delta2 = *array++;
			previous += (delta2 << 31) + (delta & MAX_ENCODABLE);
		}
		result[i] = previous;
	}

	return result;
} // end of decompressNone(...)


byte * compressPforDelta(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < FANCY_COMPRESSION_THRESHOLD)
		return compressVByte(uncompressed, listLength, byteLength);

	// count the number of deltas in each bit bucket
	int buckets[64];
	memset(buckets, 0, sizeof(buckets));
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1] - 1;
		buckets[getBitCnt(delta)]++;
	}

	int shift = 1;
	int cumulativeSum = 0;
	for (int b = 0; b < 64; b++) {
		cumulativeSum += buckets[b];
		if (cumulativeSum > listLength * 0.95) {
			shift = b;
			break;
		}
	}
	if (shift > 31)
		return compressVByte(uncompressed, listLength, byteLength);
	offset mask = (ONE << shift) - 1;

	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 8 + 32);
	result[0] = COMPRESSION_PFORDELTA;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);
	result[bytePtr++] = shift;
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	vector<int> exceptions;

	// encode the "shift" least-significant bits of every delta-value
	offset previous = uncompressed[0];
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous += delta + 1;
    
		bitBuffer |= (delta & mask) << bitsInBuffer;
    bitsInBuffer += shift;
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}

		if (delta > mask)
			exceptions.push_back(i);
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;

	// take care of the exceptions
	bytePtr += encodeVByte32(exceptions.size(), &result[bytePtr]);
  int previousException = 0;
	for (int i = 0; i < exceptions.size(); i++) {
		int posDelta = exceptions[i] - previousException;
		bytePtr += encodeVByte32(posDelta, &result[bytePtr]);
		previousException += posDelta;
		offset delta = uncompressed[exceptions[i]] - uncompressed[exceptions[i] - 1] - 1;
		bytePtr += encodeVByteOffset(delta >> shift, &result[bytePtr]);
	}

	if (PAD_ENCODED_LIST_FOR_OVERREADING)
		bytePtr += 7; // so we can over-read in the decoder

	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressPforDelta(offset*, int, int*)


offset * decompressPforDelta(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_PFORDELTA, &listLen, &bytePtr, outBuf);
	*listLength = listLen;
	int shift = compressed[bytePtr++];
	offset mask = (ONE << shift) - 1;
	
	offset *uncompressed = result;
	bytePtr += decodeVByteOffset(uncompressed++, &compressed[bytePtr]);

	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	offset previous = result[0];
	int separator = 1;
	while (bytePtr & 3) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

#if __BYTE_ORDER == __LITTLE_ENDIAN
	separator = MAX(1, listLen - 32);
	int pos = 1;
	if (shift <= 12) {
		for (pos = 1; pos < separator - 4; pos += 4) {
			while (bitsInBuffer < 48) {
				offset chunk = *((uint16_t*)&compressed[bytePtr]);
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 16;
				bytePtr += 2;
			}

  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;
  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;
  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;
  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;

  		bitsInBuffer -= shift * 4;
		}
	}
	else if (shift <= 16) {
		for (pos = 1; pos < separator - 3; pos += 3) {
			while (bitsInBuffer < 48) {
				offset chunk = *((uint16_t*)&compressed[bytePtr]);
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 16;
				bytePtr += 2;
			}

  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;
  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;
  		*uncompressed++ = (bitBuffer & mask);
  		bitBuffer >>= shift;

  		bitsInBuffer -= shift * 3;
		}
	}
	for (; pos < separator; pos++) {
		while (bitsInBuffer < 48) {
			offset chunk = *((uint16_t*)&compressed[bytePtr]);
			bitBuffer |= (chunk << bitsInBuffer);
			bitsInBuffer += 16;
			bytePtr += 2;
		}

 		*uncompressed++ = (bitBuffer & mask);
 		bitBuffer >>= shift;
 		bitsInBuffer -= shift;
	}
#endif
	// get the first "shift" bits for each delta-value
	for (int i = separator; i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		*uncompressed++ = (bitBuffer & mask);
		bitBuffer >>= shift;
		bitsInBuffer -= shift;
	}

	// put back any unused bits in the bitBuffer
	bytePtr -= bitsInBuffer / 8;

	// take care of the exceptions
	int32_t numExceptions;
	bytePtr += decodeVByte32(&numExceptions, &compressed[bytePtr]);
  int32_t exceptionPos = 0;
	while (--numExceptions >= 0) {
		int32_t posDelta;
		bytePtr += decodeVByte32(&posDelta, &compressed[bytePtr]);
		exceptionPos += posDelta;
		offset delta;
		bytePtr += decodeVByteOffset(&delta, &compressed[bytePtr]);
    result[exceptionPos] += (delta << shift);
	}

	// transform the deltas back into postings
	for (int i = 1; i < listLen; i++)
		result[i] += result[i - 1] + 1;

  return result;
}


byte * compressRice(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < 8)
		return compressVByte(uncompressed, listLength, byteLength);

	// Compute the optimal split parameters b_A and b_W according to:
	//   b_A = \lceil -log(2 - p) / log(1 - p) \rceil
	//   b_W = 2^{\lfloor log((N - f) / f) \rfloor}
	// See MG for details.
	double N = uncompressed[listLength - 1] - uncompressed[0] + 2;
	double f = listLength;
	double p = f / N;
#if 0
	offset b_A = (offset)(-log(2 - p) / log(1 - p) + 1.0);
	int shift = 1;
	while ((ONE << shift) < b_A)
		shift++;
	offset mask = (ONE << shift) - 1;
#else
	int shift = 0;
	while ((ONE << (shift + 1)) <= (N - f) / f)
		shift++;
	offset mask = (ONE << shift) - 1;
#endif

	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 8 + 32);
	result[0] = COMPRESSION_RICE;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);
	result[bytePtr++] = shift;
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset previous = uncompressed[0];
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous += delta + 1;
		
		bitBuffer |= ((delta & mask) << bitsInBuffer);
		bitsInBuffer += shift;
		delta >>= shift;
		if (delta + bitsInBuffer > 60) {
			for (int i = 0; i < delta; i++) {
				if (bitsInBuffer >= 8) {
					result[bytePtr++] = bitBuffer;
					bitBuffer >>= 8;
					bitsInBuffer -= 8;
				}
				bitBuffer |= (ONE << bitsInBuffer);
				bitsInBuffer++;
			}
		}
		else
			for (int i = 0; i < delta; i++)
				bitBuffer |= (ONE << (bitsInBuffer++));
		bitsInBuffer++;

		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;
	
	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressRice(offset*, int, int*)


offset * decompressRice(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	if (!lookAheadInitialized_GAMMA)
		initializeLookAhead_GAMMA();

	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_RICE, &listLen, &bytePtr, outBuf);
	*listLength = listLen;
	int shift = compressed[bytePtr++];
	offset mask = (ONE << shift) - 1;
	
	offset *uncompressed = result;
	bytePtr += decodeVByteOffset(uncompressed++, &compressed[bytePtr]);

	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	offset previous = result[0];
	int separator = 1;
	while (bytePtr & 3) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

#if __BYTE_ORDER == __LITTLE_ENDIAN
	separator = MAX(1, listLen - 32);
	if (shift <= 24) {
		for (int i = 1; i < separator; i++) {
			if (bitsInBuffer < 32) {
				offset chunk = *((uint32_t*)&compressed[bytePtr]);
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 32;
				bytePtr += 4;
			}

			offset delta = (bitBuffer & mask);
			bitBuffer >>= shift;
			bitsInBuffer -= shift;
			int temp;
			offset rest = 0;
			while ((temp = whereIsFirstZeroBit[bitBuffer & 255]) > 8) {
				rest += 8;
				bitBuffer >>= 8;
				bitsInBuffer -= 8;
				if (bitsInBuffer < 32) {
					offset chunk = *((uint32_t*)&compressed[bytePtr]);
					bitBuffer |= (chunk << bitsInBuffer);
					bitsInBuffer += 32;
					bytePtr += 4;
				}
			}
			bitBuffer >>= temp;
			bitsInBuffer -= temp;
			rest += temp - 1;

			*uncompressed++ = previous = previous + delta + (rest << shift) + 1;
		}
	}
	else {
		for (int i = 1; i < separator; i++) {
			while (bitsInBuffer < 48) {
				offset chunk = *((uint16_t*)&compressed[bytePtr]);
				bitBuffer |= (chunk << bitsInBuffer);
				bitsInBuffer += 16;
				bytePtr += 2;
			}

			offset delta = (bitBuffer & mask);
			bitBuffer >>= shift;
			bitsInBuffer -= shift;
			int temp;
			offset rest = 0;
			while ((temp = whereIsFirstZeroBit[bitBuffer & 255]) > 8) {
				rest += 8;
				bitBuffer >>= 8;
				bitsInBuffer -= 8;
				if (bitsInBuffer < 48) {
					offset chunk = *((uint16_t*)&compressed[bytePtr]);
					bitBuffer |= (chunk << bitsInBuffer);
					bitsInBuffer += 16;
					bytePtr += 2;
				}
			}
			bitBuffer >>= temp;
			bitsInBuffer -= temp;
			rest += temp - 1;

			*uncompressed++ = previous = previous + delta + (rest << shift) + 1;
		}
	}
#endif
	for (int i = separator; i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		offset delta = (bitBuffer & mask);
		bitBuffer >>= shift;
		bitsInBuffer -= shift;

		int temp;
		offset rest = 0;
		if ((temp = whereIsFirstZeroBit[bitBuffer & 255]) < 8) {
			bitBuffer >>= temp;
			bitsInBuffer -= temp;
			rest = temp - 1;
		}
		else {
			do {
				temp = whereIsFirstZeroBit[bitBuffer & 255] - 1;
				if (bitsInBuffer <= temp) {
					offset nextByte = compressed[bytePtr++];
					bitBuffer |= (nextByte << bitsInBuffer);
					bitsInBuffer += 8;
					temp = whereIsFirstZeroBit[bitBuffer & 255] - 1;
				}
				bitBuffer >>= temp;
				bitsInBuffer -= temp;
				rest += temp;
			} while (temp >= 8);
			bitBuffer >>= 1;
			bitsInBuffer -= 1;
		}

		*uncompressed++ = previous = previous + delta + (rest << shift) + 1;
	}

	return result;
} // end of decompressRice(byte*, int, int*)


byte * compressGolomb(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < 8)
		return compressVByte(uncompressed, listLength, byteLength);

	// Compute the optimal split parameters b_A and b_W according to:
	//   b_A = \lceil -log(2 - p) / log(1 - p) \rceil
	//   b_W = 2^{\lfloor log((N - f) / f) \rfloor}
	// See MG for details.
	double N = uncompressed[listLength - 1] - uncompressed[0] + 2;
	double f = listLength;
	double p = f / N;
	offset b_A = (offset)(-log(2 - p) / log(1 - p) + 1.0);

	int shift = 1;
	while ((ONE << shift) < b_A)
		shift++;
	offset middle = (ONE << (shift - 1));
	offset cutOff = ((ONE << shift) - b_A);
	offset right = ((ONE << shift) - (b_A - middle));

	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 8 + 32);
	result[0] = COMPRESSION_GOLOMB;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);
	bytePtr += encodeVByteOffset(b_A, &result[bytePtr]);
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	offset bitBuffer = 0;
	int bitsInBuffer = 0;

	offset previous = uncompressed[0];
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - previous - 1;
		previous += delta + 1;
		offset binaryPart = (delta % b_A);
		offset unaryPart = (delta / b_A);

		// encode binary part; use 1 bit less for small numbers
		if (binaryPart < cutOff) {
			bitBuffer |= (binaryPart << bitsInBuffer);
			bitsInBuffer += shift - 1;
		}
		else {
			if (binaryPart >= middle)
				binaryPart = right + (binaryPart - middle);
			bitBuffer |= (binaryPart << bitsInBuffer);
			bitsInBuffer += shift;
		}

		// encode unary part		
		if (unaryPart + bitsInBuffer > 60) {
			for (int i = 0; i < unaryPart; i++) {
				if (bitsInBuffer >= 8) {
					result[bytePtr++] = bitBuffer;
					bitBuffer >>= 8;
					bitsInBuffer -= 8;
				}
				bitBuffer |= (ONE << (bitsInBuffer++));
			}
		}
		else
			for (int i = 0; i < unaryPart; i++)
				bitBuffer |= (ONE << (bitsInBuffer++));
		bitsInBuffer++;

		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;
	
	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressGolomb(offset*, int, int*)


offset * decompressGolomb(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	if (!lookAheadInitialized_GAMMA)
		initializeLookAhead_GAMMA();

	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_GOLOMB, &listLen, &bytePtr, outBuf);
	*listLength = listLen;
	offset b_A = 0;
	bytePtr += decodeVByteOffset(&b_A, &compressed[bytePtr]);
	int shift = 1;
	while ((ONE << shift) < b_A)
		shift++;
	offset middle = (ONE << (shift - 1));
	offset cutOff = ((ONE << shift) - b_A);
	offset right = ((ONE << shift) - (b_A - middle));
	offset *uncompressed = result;
	bytePtr += decodeVByteOffset(uncompressed++, &compressed[bytePtr]);

	// initialize bit buffer and word-align its contents
	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	offset previous = result[0];
	int separator = 1;
	while (bytePtr & 3) {
		offset chunk = compressed[bytePtr++];
		bitBuffer |= (chunk << bitsInBuffer);
		bitsInBuffer += 8;
	}

	for (int i = separator; i < listLen; i++) {
		while ((bitsInBuffer < 56) && (bytePtr < byteLength)) {
			offset nextByte = compressed[bytePtr++];
			bitBuffer |= (nextByte << bitsInBuffer);
			bitsInBuffer += 8;
		}

		offset binaryPart = (bitBuffer & ((ONE << (shift - 1)) - 1));
		if (binaryPart < cutOff) {
			bitBuffer >>= (shift - 1);
			bitsInBuffer -= (shift - 1);
		}
		else {
			binaryPart = (bitBuffer & ((ONE << shift) - 1));
			if (binaryPart >= right)
				binaryPart = middle + (binaryPart - right);
			bitBuffer >>= shift;
			bitsInBuffer -= shift;
		}

		int temp;
		offset unaryPart = 0;
		if ((temp = whereIsFirstZeroBit[bitBuffer & 255]) < 8) {
			bitBuffer >>= temp;
			bitsInBuffer -= temp;
			unaryPart = temp - 1;
		}
		else {
			do {
				temp = whereIsFirstZeroBit[bitBuffer & 255] - 1;
				if (bitsInBuffer <= temp) {
					offset nextByte = compressed[bytePtr++];
					bitBuffer |= (nextByte << bitsInBuffer);
					bitsInBuffer += 8;
					temp = whereIsFirstZeroBit[bitBuffer & 255] - 1;
				}
				bitBuffer >>= temp;
				bitsInBuffer -= temp;
				unaryPart += temp;
			} while (temp >= 8);
			bitBuffer >>= 1;
			bitsInBuffer -= 1;
		}

		*uncompressed++ = previous = previous + binaryPart + (unaryPart * b_A) + 1;
	}

	return result;
} // end of decompressGolomb(byte*, int, int*, offset*)


byte * compressRice_SI(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < 8)
		return compressVByte(uncompressed, listLength, byteLength);

	// collect distribution statistics
	int freqs[40];
	int maxBitCnt = 2;
	memset(freqs, 0, sizeof(freqs));
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1] - 1;
		int bitCnt = getBitCnt(delta);
		if (bitCnt > 39)
			return compressVByte(uncompressed, listLength, byteLength);
		freqs[bitCnt]++;
		if (bitCnt > maxBitCnt)
			maxBitCnt = bitCnt;
	}

	// find an upper bound for the optimal value of split2, the second split point
	// between binary and unary encoding
	int covered = listLength;
	int unaryBitCnt = 0;
	while (unaryBitCnt * 2 + freqs[maxBitCnt] * 2 < covered) {
		covered -= freqs[maxBitCnt];
		unaryBitCnt = unaryBitCnt * 2 + freqs[maxBitCnt] * 2;
		if (--maxBitCnt < 5)
			break;
	}

	// do a brute-force search for the optimal parameter settings
	int split2 = 32;
	int split1 = 24;
	int bestBitCnt = 1000000000;
	for (int sp2 = maxBitCnt; sp2 > 0; sp2--) {
		int bestInThisIteration = 1000000000;
		for (int sp1 = sp2 - 1; sp1 >= 0; sp1--) {
			int bitCountHere = 0;
			for (int i = 1; i < listLength; i++) {
				offset delta = uncompressed[i] - uncompressed[i - 1] - 1;
				long unary1 = (delta >> sp1) + 1;
				long unary2 = (delta >> sp2) + 1;
				if (sp1 + unary1 <= sp2)
					bitCountHere += sp1 + unary1;
				else
					bitCountHere += sp2 + unary2;
				if (bitCountHere > 1000000000)
					break;
			}
			if (bitCountHere > bestInThisIteration)
				break;
			if (bitCountHere < bestBitCnt) {
				bestBitCnt = bitCountHere;
				split1 = sp1;
				split2 = sp2;
			}
			bestInThisIteration = bitCountHere;
		}
		if (bestInThisIteration > bestBitCnt)
			break;
	}

	// write compression header
	byte *result = (byte*)malloc(listLength * 8 + 32);
	result[0] = COMPRESSION_RICE_SI;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	// initialize bit buffer with the two split points just obtained
	offset bitBuffer = (split1 + (split2 << 6));
	int bitsInBuffer = 12;

	offset mask1 = (ONE << split1) - 1;
	offset mask2 = (ONE << split2) - 1;

	// compress data
	for (int i = 1; i < listLength; i++) {
		offset delta = uncompressed[i] - uncompressed[i - 1] - 1;

		long unary1 = (delta >> split1) + 1;
		long unary2 = (delta >> split2) + 1;
		if (split1 + unary1 <= split2) {
			bitsInBuffer++;
			bitBuffer |= ((delta & mask1) << bitsInBuffer);
			bitsInBuffer += split1;
			bitBuffer |= (ONE << (unary1 - 1));
			bitsInBuffer += unary1;
		}
		else {
			bitBuffer |= (ONE << bitsInBuffer);
			bitsInBuffer++;
			bitBuffer |= ((delta & mask2) << bitsInBuffer);
			bitsInBuffer += split2;
			while (unary2 > 8) {
				if (bitsInBuffer >= 8) {
					result[bytePtr++] = bitBuffer;
					bitBuffer >>= 8;
					bitsInBuffer -= 8;
				}
				bitsInBuffer += 8;
				unary2 -= 8;
			}
			bitBuffer |= (ONE << (unary2 - 1));
			bitsInBuffer += unary2;
		}
		
		while (bitsInBuffer >= 8) {
			result[bytePtr++] = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		result[bytePtr++] = bitBuffer;
	
	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressRice_SI(offset*, int, int*)


offset * decompressRice_SI(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_RICE_SI, &listLen, &bytePtr, outBuf);
	*listLength = listLen;
	return result;
} // end of decompressRice_SI(byte*, int, int*, offset*)


byte * compressVByte(offset *uncompressed, int listLength, int *byteLength) {
	int postingsConsumed;
	byte *result =
		compressVByte(uncompressed, listLength, 250000000, byteLength, &postingsConsumed);
	return result;
} // end of compressVByte(offset*, int, int*)


byte * compressVByte(offset *uncompressed, int listLength, int maxOutputSize,
		int *byteLength, int *postingsConsumed) {
	if (maxOutputSize < 32) {
		*byteLength = 0;
		*postingsConsumed = 0;
		return NULL;
	}

	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(MIN(maxOutputSize, listLength * 7 + 16));
	result[0] = COMPRESSION_VBYTE;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);

	offset delta = uncompressed[0];
	while (delta >= 128) {
		result[bytePtr++] = 128 + (delta & 127);
		delta >>= 7;
	}
	result[bytePtr++] = delta;

	bool allFitInto7Bits = true;
	int elementsProcessed = 0;
	for (int i = 1; i < listLength; i++) {
		// if we are out of memory (pessimistic estimation here): STOP!
		if (bytePtr + 7 > maxOutputSize)
			break;

		// encode i-th element of list
		delta = uncompressed[i] - uncompressed[i - 1];
		while (delta >= 128) {
			allFitInto7Bits = false;
			result[bytePtr++] = 128 + (delta & 127);
			delta >>= 7;
		}
		result[bytePtr++] = delta;
		elementsProcessed++;
	}

	// set a flag in case all delta values fit into 7 bits each; if that's the
	// case, we can use a more efficient decoding procedure
	if (allFitInto7Bits)
		result[0] |= 128;

	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	*postingsConsumed = elementsProcessed;
	return result;
} // end of compressVByte(...)


offset * decompressVByte(byte *compressed, int byteLength, int *listLength,
		offset *outputBuffer, offset startOffset) {
	int listLen, bPtr;
	offset *result =
		readHeader(compressed, COMPRESSION_VBYTE, &listLen, &bPtr, outputBuffer);
	*listLength = listLen;

	byte *bytePtr = &compressed[bPtr];
	offset *output = result;
	offset *limit = &result[listLen];

	bool allFitInto7Bits = (compressed[0] >= 128);
	if (allFitInto7Bits) {
		// if the encoder tells us that all delta values fit into 7 bits, then
		// we use a slightly faster decoding routine here
		offset current;
		bytePtr += decodeVByteOffset(&current, bytePtr);
		*output++ = current;
		while (output != limit) {
			current += *bytePtr++;
			*output++ = current;
		}
	}
	else {
		// otherwise, we switch to the default vByte decoder
		offset dummy, current = startOffset;
		unsigned int shift = 0;
		while (output != limit) {
			byte b;
			do {
				b = *bytePtr++;
				dummy = (b & 127);
				current += (dummy << shift);
				shift += 7;
			} while (b & 128);
			*output++ = current;
			shift = 0;
		}
	}

	return result;
} // end of decompressVByte(byte*, int, int*, offset*, offset)


byte * compressGroupVarInt(offset *uncompressed, int listLength, int *byteLength) {
	if (listLength < 9)
		return compressVByte(uncompressed, listLength, byteLength);

	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 5 + 32);
	result[0] = COMPRESSION_GROUPVARINT;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);

	// encode the first posting, which may be larger than 1^32
	bytePtr += encodeVByteOffset(uncompressed[0], &result[bytePtr]);

	// encode chunks of 4 postings at a time
	bool allFitInto8Bits = true;
	int inPos = 1;
	while (inPos <= listLength - 4) {
		byte *selector = &result[bytePtr++];
		*selector = 0;
		for (int i = 0; i < 4; i++) {
			const offset delta = uncompressed[inPos + i] - uncompressed[inPos + i - 1];
			int numBytes = 0;
			while (delta >> (numBytes * 8) > 0)
				numBytes++;
			if (numBytes > 1)
				allFitInto8Bits = false;
			if (numBytes > 4) {
				// doesn't fit into 32 bits; fall back to vByte
				free(result);
				return compressVByte(uncompressed, listLength, byteLength);
			}
			// add numBytes to selector and put delta into payload
			*selector |= ((numBytes - 1) << (i * 2));
			*(uint32_t*)&result[bytePtr] = delta;
			bytePtr += numBytes;
		}
		inPos += 4;
	}

	const int endOfGroups = bytePtr;

	// encode the remaining 0-3 postings
	while (inPos < listLength) {
		bytePtr += encodeVByteOffset(uncompressed[inPos] - uncompressed[inPos - 1],
		                             &result[bytePtr]);
		inPos++;
	}

	if (PAD_ENCODED_LIST_FOR_OVERREADING) {
		while (bytePtr < endOfGroups + 3)
			++bytePtr;  // so we can over-read in the decoder
	}

	// set a flag in case all delta values fit into 7 bits each; if that's the
	// case, we can use a more efficient decoding procedure
	if (allFitInto8Bits)
		result[0] |= 128;

	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compressGroupVarInt(...)


offset * decompressGroupVarInt(byte *compressed, int byteLength, int *listLength, offset *outBuf) {
	if (!groupVarInt_Initialized) {
		// initialize the mask lookup table
		initializeGroupVarInt();
	}

	// parse the header
	int listLen, bytePtr;
	offset *result =
		readHeader(compressed, COMPRESSION_GROUPVARINT, &listLen, &bytePtr, outBuf);
	*listLength = listLen;

	bool allFitInto8Bits = (compressed[0] >= 128);

	// parse the first posting
	compressed = compressed + bytePtr;
	compressed += decodeVByteOffset(&result[0], compressed);
	offset current = result[0];
	offset *outPtr = &result[1];

	const uint32_t mask[4] = { 0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFFFF };

	// decode chunks of 4 postings at a time
	const offset *limit = outPtr + (((listLen - 1) / 4) * 4);
	if (allFitInto8Bits) {
		while (outPtr != limit) {
			compressed++;
			current += *compressed++;
			*outPtr++ = current;
			current += *compressed++;
			*outPtr++ = current;
			current += *compressed++;
			*outPtr++ = current;
			current += *compressed++;
			*outPtr++ = current;
		}
	} else {
		while (outPtr != limit) {
#if 0
			// Decode using an offset/mask lookup table.
			const GroupVarIntHelper *helper = &groupVarIntLookupTable[*compressed];
			current += *((uint32_t*)(compressed + 1)) & helper->mask1;
			outPtr[0] = current;
			current += *((uint32_t*)(compressed + helper->offset1)) & helper->mask2;
			outPtr[1] = current;
			current += *((uint32_t*)(compressed + helper->offset2)) & helper->mask3;
			outPtr[2] = current;
			current += *((uint32_t*)(compressed + helper->offset3)) & helper->mask4;
			outPtr[3] = current;
			compressed += helper->offset4;
			outPtr += 4;
#else
			// Decode using simple arithmetic.
			const uint32_t selector = *compressed++;
			const uint32_t selector1 = (selector & 3);
			current += *((uint32_t*)(compressed)) & mask[selector1];
			*outPtr++ = current;
			compressed += selector1 + 1;
			const uint32_t selector2 = ((selector >> 2) & 3);
			current += *((uint32_t*)(compressed)) & mask[selector2];
			*outPtr++ = current;
			compressed += selector2 + 1;
			const uint32_t selector3 = ((selector >> 4) & 3);
			current += *((uint32_t*)(compressed)) & mask[selector3];
			*outPtr++ = current;
			compressed += selector3 + 1;
			const uint32_t selector4 = (selector >> 6);
			current += *((uint32_t*)(compressed)) & mask[selector4];
			*outPtr++ = current;
			compressed += selector4 + 1;
		}
#endif
	}
	
	// decode the remaining 0-3 postings
	limit = result + listLen;
	while (outPtr != limit) {
		offset delta;
		compressed += decodeVByteOffset(&delta, compressed);
		current += delta;
		*outPtr++ = current;
	}
	return result;
}

byte * compress7Bits(offset *uncompressed, int listLength, int *byteLength) {
	// allocate space and store compression mode and list length in the header
	byte *result = (byte*)malloc(listLength * 7 + 256);
	result[0] = COMPRESSION_VBYTE;
	int bytePtr = 1 + encodeVByte32(listLength, &result[1]);

	offset previous = 0;
	offset bitBuffer = 0;
	int bitsInBuffer = 0;
	byte *compressed = result;
	for (int i = 0; i < listLength; i++) {
		offset delta = uncompressed[i] - previous;
		previous += delta;
		while (delta >= 64) {
			bitBuffer += (64 | (delta & 63)) << bitsInBuffer;
			bitsInBuffer += 7;
			delta = delta >> 6;
		}
		bitBuffer += (delta << bitsInBuffer);
		bitsInBuffer += 7;
		while (bitsInBuffer >= 8) {
			*compressed++ = bitBuffer;
			bitBuffer >>= 8;
			bitsInBuffer -= 8;
		}
	}
	if (bitsInBuffer > 0)
		*compressed++ = bitBuffer;

	result = (byte*)realloc(result, bytePtr);
	*byteLength = bytePtr;
	return result;
} // end of compress7Bits(offset*, int, int*)


long long bytesDecompressed = 0;


offset * decompressList(byte *compressed, int byteLen, int *listLen, offset *outBuf) {
	bytesDecompressed += byteLen;
	int compressionMode = (compressed[0] & 127);
	if (compressionMode == COMPRESSION_VBYTE)
		return decompressVByte(compressed, byteLen, listLen, outBuf);
	else
		return decompressorForID[compressionMode](compressed, byteLen, listLen, outBuf);
} // end of decompressList(byte*, int, int*, offset*)


int extractCompressionModeFromList(byte *compressed) {
	int compressionMode = (compressed[0] & 127);
	return compressionMode;
} // end of getCompressionModeForCompressedList(byte*)


offset * decompressList(byte *compressed, int byteLength, int *listLength,
		offset *outputBuffer, offset startOffset) {
	// determine compression mode
	int compressionMode = compressed[0];

	if (compressionMode == COMPRESSION_VBYTE)
		return decompressVByte(compressed, byteLength, listLength, outputBuffer, startOffset);

	// select the appropriate decompression algorithm
	offset *result = decompressList(compressed, byteLength, listLength, outputBuffer);
	if (startOffset != 0) {
		int len = *listLength;
		for (int i = 0; i < len; i++)
			outputBuffer[i] += startOffset;
	}
	return result;
} // end of decompressList(...)


byte *mergeCompressedLists(byte *firstList, int firstByteLength, byte *secondList,
		int secondByteLength, offset lastInFirst, int *newLength, int *newByteLength, bool append) {

	// list length (number of postings) of first and second list
	int firstLength, secondLength;
	bool mustFreeFirst = false, mustFreeSecond = false;

	// for longer lists, determine compression type and recompress using vbyte if necessary
	int firstCompressionMode = firstList[0];
	int secondCompressionMode = secondList[0];

	// if the first list is in the wrong encoding format, recompress it using vbyte
	if ((firstCompressionMode & 127) != COMPRESSION_VBYTE) {
		offset *postings = decompressList(firstList, firstByteLength, &firstLength, NULL);
		if (append) {
			byte *temp = compressVByte(postings, firstLength, &firstByteLength);
			memcpy(firstList, temp, firstByteLength);
			free(temp);
		}
		else {
			firstList = compressVByte(postings, firstLength, &firstByteLength);
			mustFreeFirst = true;
		}
		free(postings);
	}
	int firstPtr = 1 + decodeVByte32(&firstLength, &firstList[1]);

	// if the second list is in the wrong encoding format, recompress it using vbyte
	if ((secondCompressionMode & 127) != COMPRESSION_VBYTE) {
		offset *postings = decompressList(secondList, secondByteLength, &secondLength, NULL);
		secondList = compressVByte(postings, secondLength, &secondByteLength);
		free(postings);
		mustFreeSecond = true;
	}
	int secondPtr = 1 + decodeVByte32(&secondLength, &secondList[1]);

	// now, both lists are in vbyte format: start merging
	byte *result;
	int outPtr;

	if (append) {
		// in APPEND mode: put list 2 in the array specified by list 1
		result = firstList;
		byte temp[16];
		outPtr = 1 + encodeVByte32(firstLength + secondLength, temp);
		if (outPtr != firstPtr)
			memmove(&firstList[outPtr], &firstList[firstPtr], firstByteLength - firstPtr);
		outPtr = 1 + encodeVByte32(firstLength + secondLength, &result[1]);
		outPtr += (firstByteLength - firstPtr);
	}
	else {
		// not in APPEND mode: create new array holding the postings from both input lists
		result = (byte*)malloc(firstByteLength + secondByteLength);
		result[0] = COMPRESSION_VBYTE;
		outPtr = 1 + encodeVByte32(firstLength + secondLength, &result[1]);
		memcpy(&result[outPtr], &firstList[firstPtr], firstByteLength - firstPtr);
		outPtr += (firstByteLength - firstPtr);
	} // end else [!append]

	// extract first posting from second list
	offset firstInSecond;
	secondPtr += decodeVByteOffset(&firstInSecond, &secondList[secondPtr]);

	// re-encode the first posting from the second list as delta value relative to
	// the last posting in the first list
	offset toEncode = firstInSecond - lastInFirst;
	outPtr += encodeVByteOffset(toEncode, &result[outPtr]);

	// special treatment for vByte bit-fiddling here: if the "all gaps are
	// smaller than 128" flag is set, we have to make sure that this is
	// actually true for the whole list, not just the first part
	if (result[0] == (COMPRESSION_VBYTE | 128))
		if ((secondList[0] != result[0]) || (firstInSecond > lastInFirst + 127))
			result[0] = COMPRESSION_VBYTE;

	// copy the remainder of the second list into the new buffer and update
	// the output parameters
	memcpy(&result[outPtr], &secondList[secondPtr], secondByteLength - secondPtr);
	*newLength = (firstLength + secondLength);
	*newByteLength = outPtr + (secondByteLength - secondPtr);

	// free temporary buffers in case we recompressed the compressed posting lists
	if (mustFreeFirst)
		free(firstList);
	if (mustFreeSecond)
		free(secondList);
	return result;
} // end of mergeCompressedLists(byte*, int, byte*, int, offset, int*, int*)


int getCompressorForName(const char *name) {
	static bool initialized = false;
	static map<string,int> compressors;
	static Lockable l;
	LocalLock lock(&l);
	if (!initialized) {
		compressors["vbyte"] = COMPRESSION_VBYTE;
		compressors["golomb"] = COMPRESSION_GOLOMB;
		compressors["rice"] = COMPRESSION_RICE;
		compressors["huffman"] = COMPRESSION_LLRUN;
		compressors["llrun"] = COMPRESSION_LLRUN;
		compressors["gamma"] = COMPRESSION_GAMMA;
		compressors["delta"] = COMPRESSION_DELTA;
		compressors["pfordelta"] = COMPRESSION_PFORDELTA;
		compressors["groupvarint"] = COMPRESSION_GROUPVARINT;
		compressors["gubc"] = COMPRESSION_GUBC;
		compressors["gubcip"] = COMPRESSION_GUBCIP;
		compressors["simple9"] = COMPRESSION_SIMPLE_9;
		compressors["simple_9"] = COMPRESSION_SIMPLE_9;
		compressors["interpol"] = COMPRESSION_INTERPOLATIVE;
		compressors["interpolative"] = COMPRESSION_INTERPOLATIVE;
		compressors["interpol_si"] = COMPRESSION_INTERPOLATIVE_SI;
		compressors["huffman_direct"] = COMPRESSION_HUFFMAN_DIRECT;
		compressors["huffman_multi"] = COMPRESSION_LLRUN_MULTI;
		compressors["rice_si"] = COMPRESSION_RICE_SI;
		compressors["huffman2"] = COMPRESSION_HUFFMAN2;
		compressors["experimental"] = COMPRESSION_EXPERIMENTAL;
		compressors["none"] = COMPRESSION_NONE;
	}
	char temp[32];
	map<string,int>::iterator iter;
	int len = strlen(name);
	if ((len < 2) || (len > 30))
		goto getCompressorForName_ERROR;
	for (int i = 0; i < len; i++) {
		char c = name[i];
		if ((c >= 'A') && (c <= 'Z'))
			c |= 32;
		temp[i] = c;
	}
	temp[len] = 0;
	if ((iter = compressors.find(temp)) == compressors.end())
		goto getCompressorForName_ERROR;
	return iter->second;
getCompressorForName_ERROR:
	char msg[256];
	sprintf(msg, "Invalid compression ID: \"%s\". Assuming VBYTE.", name);
	log(LOG_ERROR, "getCompressorForName", msg);
	return COMPRESSION_VBYTE;
} // end of getCompressorForName(const char*)


