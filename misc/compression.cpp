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
 * Implementation of the lossless multi-purpose compression methods.
 *
 * author: Stefan Buettcher
 * created: 2005-05-28
 * changed: 2006-09-19
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "compression.h"
#include "../misc/alloc.h"


typedef struct {
	int16_t parent;
	int16_t firstChild;
	int16_t nextSibling;
	int16_t content;
} LZWStruct;


#define READ_ONE_BIT(bitarray, position) \
	((bitarray[position >> 3] & (1 << (position & 7))) != 0 ? 1 : 0)

#define WRITE_ONE_BIT(value, bitarray, position) { \
	if (value != 0) \
		bitarray[position >> 3] |= (1 << (position & 7)); \
	else \
		bitarray[position >> 3] &= (0xFF ^ (1 << (position & 7))); \
}

#define READ_N_BITS(result, n, bitarray, position) { \
	result = 0; \
	for (int chaosCounter = n - 1; chaosCounter >= 0; chaosCounter--) \
		result = (result << 1) + READ_ONE_BIT(bitarray, position + chaosCounter); \
}
                                                                                                         
#define WRITE_N_BITS(value, n, bitarray, position) { \
	int tempValue = value; \
	for (int chaosCounter = 0; chaosCounter < n; chaosCounter++) { \
		WRITE_ONE_BIT(tempValue & 1, bitarray, position + chaosCounter) \
		tempValue = tempValue >> 1; \
	} \
}


static void initializeTable(LZWStruct *table) {
	for (int i = 0; i < 256; i++) {
		table[i].parent = -1;
		table[i].firstChild = -1;
		table[i].nextSibling = -1;
		table[i].content = i;
	}	
} // end of initializeTable(LZWStruct*)


void compressLZW(byte *uncompressed, byte *compressed, int inSize, int *outSize, int maxOutSize) {
	*outSize = -1;
	if (maxOutSize < 16)
		return;
	int maxCharInInput = 0;
	for (int i = 0; i < inSize; i++)
		if (uncompressed[i] > maxCharInInput)
			maxCharInInput = uncompressed[i];
	if (maxCharInInput > 250)
		maxCharInInput = 256;
	LZWStruct *table = typed_malloc(LZWStruct, MAX_LZW_TABLE_SIZE);
	initializeTable(table);
	int current = -1;
	int currentTableSize = maxCharInInput + 1;
	int currentBitWidth = 1;
	while ((1 << currentBitWidth) < currentTableSize)
		currentBitWidth++;
	int32_t inPos = 0;
	int32_t bitPos = 40;
	maxOutSize *= 8;
	while (inPos < inSize) {
		if (bitPos + MAX_LZW_BITLENGTH >= maxOutSize) {
			free(table);
			return;
		}
		if (current < 0)
			current = uncompressed[inPos++];
		else {
			int16_t c = uncompressed[inPos++];
			int child = table[current].firstChild;
			while (child >= 0) {
				if (table[child].content == c)
					break;
				child = table[child].nextSibling;
			}
			if (child >= 0)
				current = child;
			else {
				WRITE_N_BITS(current, currentBitWidth, compressed, bitPos);
				bitPos += currentBitWidth;

				currentTableSize++;
				if ((1 << currentBitWidth) < currentTableSize) {
					currentBitWidth++;
					if (currentBitWidth > MAX_LZW_BITLENGTH) {
						initializeTable(table);
						currentTableSize = maxCharInInput + 1;
						currentBitWidth = 1;
						while ((1 << currentBitWidth) < currentTableSize)
							currentBitWidth++;
						current = c;
						continue;
					}
				}
				table[currentTableSize - 1].content = c;
				table[currentTableSize - 1].parent = current;
				table[currentTableSize - 1].firstChild = -1;
				table[currentTableSize - 1].nextSibling = table[current].firstChild;
				table[current].firstChild = currentTableSize - 1;
				current = c;
			}
		}
	}
	free(table);
	if (current >= 0) {
		if (bitPos + MAX_LZW_BITLENGTH >= maxOutSize)
			return;
		WRITE_N_BITS(current, currentBitWidth, compressed, bitPos);
		bitPos += currentBitWidth;
	}
	memcpy(compressed, &bitPos, sizeof(bitPos));
	compressed[4] = maxCharInInput & 0xFF;
	*outSize = (bitPos + 7) / 8;
} // end of compressLZW(...)


void decompressLZW(byte *compressed, byte *uncompressed, int *outSize, int maxOutSize) {
	*outSize = -1;
	if (maxOutSize < 16)
		return;
	int32_t inSize = 0;
	memcpy(&inSize, compressed, sizeof(int32_t));
	int maxCharInInput = compressed[4];
	if (maxCharInInput == 0)
		maxCharInInput = 256;
	int bitPos = 40;
	int outPos = 0;
	LZWStruct *table = typed_malloc(LZWStruct, MAX_LZW_TABLE_SIZE);
	initializeTable(table);
	int current, last = -1;
	int currentTableSize = maxCharInInput + 1;
	int currentBitWidth = 1;
	while ((1 << currentBitWidth) < currentTableSize)
		currentBitWidth++;

	while (bitPos < inSize) {
		if ((1 << currentBitWidth) < currentTableSize + 1) {
			currentBitWidth++;
			if (currentBitWidth > MAX_LZW_BITLENGTH) {
				initializeTable(table);
				currentTableSize = maxCharInInput + 1;
				currentBitWidth = 1;
				while ((1 << currentBitWidth) < currentTableSize)
					currentBitWidth++;
				last = -1;
			}
		}

		READ_N_BITS(current, currentBitWidth, compressed, bitPos);
		bitPos += currentBitWidth;
		assert(current <= currentTableSize);

		int newPos, length = 0;
		if (current == currentTableSize) {
			assert(last >= 0);
			for (int cur = last; cur >= 0; cur = table[cur].parent)
				length++;
			if (outPos + length >= maxOutSize) {
				free(table);
				return;
			}
			newPos = outPos + length + 1;
			for (int cur = last; cur >= 0; cur = table[cur].parent)
				uncompressed[outPos + (--length)] = table[cur].content;
			uncompressed[newPos - 1] = uncompressed[outPos];
		}
		else {
			for (int cur = current; cur >= 0; cur = table[cur].parent)
				length++;
			if (outPos + length > maxOutSize) {
				free(table);
				return;
			}
			newPos = outPos + length;
			for (int cur = current; cur >= 0; cur = table[cur].parent)
				uncompressed[outPos + (--length)] = table[cur].content;
		}

		int16_t c = uncompressed[outPos];
		outPos = newPos;
		if (last >= 0) {
			currentTableSize++;
			while ((1 << currentBitWidth) < currentTableSize)
				currentBitWidth++;
			table[currentTableSize - 1].content = c;
			table[currentTableSize - 1].parent = last;
			table[currentTableSize - 1].firstChild = -1;
			table[currentTableSize - 1].nextSibling = table[last].firstChild;
			table[last].firstChild = currentTableSize - 1;
		}
		last = current;
	} // end while (bitPos < inSize)
	free(table);
	*outSize = outPos;
} // end of decompressLZW(...)


void compressPTR(byte *uncompressed, byte *compressed, int inSize, int *outSize, int maxOutSize) {
	*outSize = -1;
	if (maxOutSize < 16)
		return;
	int32_t inPos = 0;
	int32_t outPos = 4;
	while (inPos < inSize) {
		if (outPos >= maxOutSize - 3)
			return;
		int candidateStart = -1;
		int candidateLength = -1;
		for (int pos = inPos - 4; (pos >= 0) && (pos >= inPos - MAX_PTR_BACKWARDS); pos--)
			if (uncompressed[pos] == uncompressed[inPos]) {
				int cLen = 1;
				while (inPos + cLen < inSize) {
					if (uncompressed[pos + cLen] != uncompressed[inPos + cLen])
						break;
					cLen++;
				}
				if (cLen > candidateLength) {
					candidateStart = pos;
					candidateLength = cLen;
					if (cLen >= PTR_STOP_SEARCH)
						break;
				}
			}
		if (candidateLength < 3) {
			if (uncompressed[inPos] == 255) {
				compressed[outPos++] = uncompressed[inPos++];
				compressed[outPos++] = 0;
			}
			else
				compressed[outPos++] = uncompressed[inPos++];
		}
		else {
			if (candidateLength > 255)
				candidateLength = 255;
			compressed[outPos++] = 255;
			compressed[outPos++] = (inPos - candidateStart);
			compressed[outPos++] = candidateLength;
			inPos += candidateLength;
		}
	}
	memcpy(compressed, &outPos, sizeof(int32_t));
	*outSize = outPos;
} // end of compressPTR(...)


void decompressPTR(byte *compressed, byte *uncompressed, int *outSize, int maxOutSize) {
	*outSize = -1;
	if (maxOutSize < 8)
		return;
	int32_t inSize = 0;
	memcpy(&inSize, compressed, sizeof(int32_t));
	int inPos = 4;
	int outPos = 0;
	while (inPos < inSize) {
		if (compressed[inPos] != 255) {
			if (outPos >= maxOutSize)
				return;
			uncompressed[outPos++] = compressed[inPos++];
		}
		else if (compressed[inPos + 1] == 0) {
			if (outPos >= maxOutSize)
				return;
			uncompressed[outPos++] = compressed[inPos++];
			inPos++;
		}
		else {
			int from = outPos - compressed[inPos + 1];
			int len = compressed[inPos + 2];
			inPos += 3;
			if (outPos + len > maxOutSize)
				return;
			for (int i = 0; i < len; i++)
				uncompressed[outPos++] = uncompressed[from + i];
		}
	}
	*outSize = outPos;
} // end of decompressPTR(...)


#if 0

char uncompressed[40*1024*1024];
char compressed[16*1024*1024];
char check[40*1024*1024];

int main() {
	char line[1024];
	int size = 0;
	while (fgets(line, 1023, stdin) != NULL) {
		strcpy(&uncompressed[size], line);
		size += strlen(line) + 1;
	}
	int compressedSize;
	compressLZW((byte*)uncompressed, (byte*)compressed, size, &compressedSize, sizeof(compressed));
	printf("size = %d, compressedSize = %d\n", size, compressedSize);
	int checkSize;
	decompressLZW((byte*)compressed, (byte*)check, &checkSize, sizeof(check));
	printf("checkSize = %d\n", checkSize);
	for (int i = 0; i < size; i++) {
		assert(uncompressed[i] == check[i]);
	}
	assert(size == checkSize);
	return 0;
} // end of main()

#endif

