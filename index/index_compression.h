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
 * This header file defines the various index compression techniques that may
 * be used by our system. Some of the compression algorithms defined here and
 * implemented in index_compression.cpp are taken from the book "Managing
 * Gigabytes" by Witten, Moffat, and Bell. Others are made in Germany.
 *
 * author: Stefan Buettcher
 * created: 2004-11-11
 * changed: 2010-03-13
 **/


#ifndef __INDEX__INDEX_COMPRESSION_H
#define __INDEX__INDEX_COMPRESSION_H


#include "index_types.h"


/**
 * Header field IDs telling us what compression algorithm was used to produce
 * a given compressed posting list.
 **/
#define COMPRESSION_INVALID           0

#define COMPRESSION_GAMMA             1
#define COMPRESSION_DELTA             2
#define COMPRESSION_VBYTE             3
#define COMPRESSION_SIMPLE_9          4
#define COMPRESSION_INTERPOLATIVE     5
#define COMPRESSION_NIBBLE            6
#define COMPRESSION_LLRUN             7
#define COMPRESSION_RICE              8
#define COMPRESSION_GOLOMB            9
#define COMPRESSION_GUBC             10
#define COMPRESSION_GUBCIP           11
#define COMPRESSION_PFORDELTA        12
#define COMPRESSION_GROUPVARINT      13

#define COMPRESSION_NONE             14
#define COMPRESSION_LLRUN_MULTI      15
#define COMPRESSION_HUFFMAN_DIRECT   16
#define COMPRESSION_HUFFMAN2         17
#define COMPRESSION_INTERPOLATIVE_SI 18
#define COMPRESSION_RICE_SI          19
#define COMPRESSION_EXPERIMENTAL     20
#define COMPRESSION_BEST             21

#define COMPRESSOR_COUNT             22

#define START_OF_SIMPLE_COMPRESSORS   1
#define END_OF_SIMPLE_COMPRESSORS    13


extern long long bytesDecompressed;


/**
 * Encodes the given int value into the given buffer. Returns the number
 * of bytes needed to encode the value.
 **/
static inline int encodeVByte32(int32_t value, byte *buffer) {
	int pos = 0;
	while (value >= 128) {
		buffer[pos++] = 128 + (byte)(value & 127);
		value >>= 7;
	}
	buffer[pos++] = (byte)value;
	return pos;
} // end of encodeVByte32(int32_t, byte*)

static inline int encodeVByteOffset(offset value, byte *buffer) {
	int pos = 0;
	while (value >= 128) {
		buffer[pos++] = 128 + (byte)(value & 127);
		value >>= 7;
	}
	buffer[pos++] = (byte)value;
	return pos;
} // end of encodeVByteOffset(offset, byte*)

static inline int getVByteLength(offset value) {
	int result = 1;
	while (value >= 128) {
		value >>= 7;
		result++;
	}
	return result;
} // end of getVByteLength(offset)

/**
 * Decodes an int value from the given buffer. Puts the value into the given
 * variable ("value") and returns the number of bytes consumed from the buffer.
 **/
static inline int decodeVByte32(int32_t *value, const byte *buffer) {
	int pos = 0, shift = 0;
	uint32_t dummy, result = 0;
	byte b = buffer[pos++];
	while (b >= 128) {
		dummy = (b & 127);
		result += (dummy << shift);
		shift += 7;
		b = buffer[pos++];
	}
	dummy = b;
	result += (dummy << shift);
	*value = result;
	return pos;
} // end of decodeVByte32(int32_t*, byte*)

static inline int decodeVByteOffset(offset *value, const byte *buffer) {
	int pos = 0, shift = 0;
	offset dummy, result = 0;
	byte b = buffer[pos++];
	while (b >= 128) {
		dummy = (b & 127);
		result += (dummy << shift);
		shift += 7;
		b = buffer[pos++];
	}
	dummy = b;
	result += (dummy << shift);
	*value = result;
	return pos;
} // end of decodeVByteOffset(offset*, byte*)


/** Returns the compression method used to produce the given compressed array. **/
static inline int getCompressionMethod(byte *compressed) {
	return compressed[0];
}


/**
 * Functions of the Compressor type take a list of (non-descending) offset values
 * and the length of the list as arguments and return a pointer to a byte array
 * that contains the compressed representation of the array. The value that is
 * pointed at by the int pointer will contain the byte length of the return array.
 **/
typedef byte* (*Compressor)(offset*, int, int*);

/**
 * Functions of the Decompressor type take a byte array that contains a compressed
 * offset list and the length of the array as arguments. They return a pointer to
 * an offset array that contains the uncompressed list. The value pointed at by
 * the int pointer will contain the number of offsets in the list.
 * The fourth parameter can be used to explicitly specify the output buffer where
 * the decompressed postings will be stored. This helps us reduce the amount of
 * memory copying a bit. If NULL, a new buffer will be allocated.
 **/
typedef offset* (*Decompressor)(byte*, int, int*, offset*);


/**
 * Front-codes the given "plain" string, relative to the "reference" string.
 * Stores the result in "compressed". Returns the number of bytes consumed by
 * the front-coded version of the string.
 **/
int encodeFrontCoding(const char *plain, const char *reference, byte *compressed);

/** Counterpart to encodeFrontCoding. **/
int decodeFrontCoding(const byte *compressed, const char *reference, char *plain);


/** The following structure is used by all Huffman-type compression algorithms. **/
struct HuffmanStruct {
	int32_t id;
	int32_t frequency;
	int32_t codeLength;
	int32_t code;
};


/**
 * Takes a sequence of Huffman symbol descriptors (with id and frequency filled
 * in) and computes the Huffman code lengths of all symbols.
 **/
void doHuffman(HuffmanStruct *array, int length);


/**
 * Takes a sequence of Huffman symbol descriptors (with codeLength filled in)
 * and computes the canonical Huffman code for the given codeLength data.
 **/
void computeHuffmanCodesFromCodeLengths(HuffmanStruct *array, int length);


/**
 * Takes a Huffman tree and transforms it so that no single code word is
 * longer than "maxCodeLen" bits.
 **/
void restrictHuffmanCodeLengths(HuffmanStruct *array, int length, int maxCodeLen);


/** Sorts the elements of the Huffman model by their "id" component (ascending). **/
void sortHuffmanStructsByID(HuffmanStruct *array, int length);


/** This is the no-compression compression algorithm. **/
byte * compressNone(offset *uncompressed, int listLen, int *byteLen);
offset * decompressNone(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/** Golomb coding. **/
byte * compressGolomb(offset *uncompressed, int listLen, int *byteLen);
offset * decompressGolomb(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/** Rice coding (Golomb where b is a power of 2). **/
byte * compressRice(offset *uncompressed, int listLen, int *byteLen);
offset * decompressRice(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/** Variation of Rice for schema-independent posting lists. **/
byte * compressRice_SI(offset *uncompressed, int listLen, int *byteLen);
offset * decompressRice_SI(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * This is the default Gamma compression in which we store offset delta values
 * (difference between two subsequent list elements) and each difference is
 * preceded by a header of the form "111110", where the length of the header
 * tells us how many bits we need to represent the difference.
 **/
byte * compressGamma(offset *uncompressed, int listLen, int *byteLen);
offset * decompressGamma(byte *compressed, int byteLen, int *listLen, offset *outBuf);

byte * compressDelta(offset *uncompressed, int listLen, int *byteLen);
offset * decompressDelta(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * This is interpolative coding, as described by Moffat et al. It includes the
 * obvious optimization where codewords in the center of a gap are encoded using
 * 1 bit less than codewords close to the left or right edge.
 **/
byte * compressInterpolative(offset *uncompressed, int listLen, int *byteLen);
offset * decompressInterpolative(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * Same as above, but for compressing schema-independent posting lists.
 **/
byte * compressInterpolative_SI(offset *uncompressed, int listLen, int *byteLen);
offset * decompressInterpolative_SI(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * This compression method is a byte-based technique for increased encoding
 * and decoding efficiency. Each byte in the encoded posting list holds 7
 * bits of d-gap data. The 8th bit is used to indicate whether the current
 * byte is the last data byte or whether there are more to follow.
 **/
byte * compressVByte(offset *uncompressed, int listLength, int *byteLength);
byte * compressVByte(offset *uncompressed, int listLength, int maxOutputSize,
                     int *byteLength, int *postingsConsumed);
offset * decompressVByte(byte *compressed, int byteLength, int *listLength,
                         offset *outputBuffer, offset startOffset);

static inline offset * decompressVByte(byte *compr, int bLen, int *listLen, offset *outBuf) {
	return decompressVByte(compr, bLen, listLen, outBuf, 0);
}


/**
 * A nibble-aligned compression theme in which the first nibble of every
 * posting is used to tell the decompressor the number of following nibbles
 * used to encode the posting.
 **/
byte * compressNibble(offset *uncompressed, int listLen, int *byteLen);
offset * decompressNibble(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * These are compression/decompression routines for the Simple-9 method descibed
 * by Anh and Moffat: "Inverted Index Compression Using Word-Aligned Binary Codes"
 * (Information Retrieval, 8, 151-166, 2005).
 **/
byte * compressSimple9(offset *uncompressed, int listLen, int *byteLen);
offset * decompressSimple9(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * This compression technique is similar to Gamma encoding, except that the
 * prefix of every code word (encoded posting) is represented by a Huffman
 * code instead of unary. Decompression performance is improved by making
 * use of a bit buffer (to amortize bit operations) and a code look-ahead
 * buffer (to determine the size of the next code word).
 **/
byte * compressLLRun(offset *uncompressed, int listLen, int *byteLen);
offset * decompressLLRun(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * Compresses/decompresses a given postings list according to a precomputed
 * Huffman model. The model must have entries for at least 40 elements.
 **/
byte * compressLLRunWithGivenModel(
		offset *uncompressed, int listLen, HuffmanStruct *model, int *byteLen);
offset * decompressLLRunWithGivenModel(
		byte *compressed, int byteLen, HuffmanStruct *model, int *listLen, offset *outBuf);


/**
 * This is the same as compressHuffman, except that instead of only using a
 * single Huffman tree, multiple Huffman trees are used, depending on the bit
 * length of the previous gap.
 **/
byte * compressLLRunMulti(offset *uncompressed, int listLen, int *byteLen);


/**
 * HUFFMAN_DIRECT can be used to encode sequences of small positive integers
 * (< 64 or so). It does not work if elements of the sequence are large
 * (e.g., arbitrary postings).
 **/
byte * compressHuffmanDirect(offset *uncompressed, int listLen, int *byteLen);
offset * decompressHuffmanDirect(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * This is a Huffman-based compression technique that takes into account the
 * special structure of postings in a frequency index (DOCUMENT_LEVEL_INDEXING)
 * and encodes the lower-most N bits independently of the rest of the posting.
 **/
byte * compressHuffman2(offset *uncompressed, int listLen, int *byteLen);
offset * decompressHuffman2(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * This is a generalized version of Gamma encoding. In Gamma, every bit of an
 * encoded posting is preceded by a header bit. In GUBC (generalized unaligned
 * binary coding), a header bit corresponds to N body bits, where the ratio N
 * is determined at compression time, minimizing the total space consumption.
 **/
byte * compressGUBC(offset *uncompressed, int listLen, int *byteLen);
offset * decompressGUBC(byte *compressed, int byteLen, int *listLen, offset *outBuf);


/**
 * GUBCIP (generalized unaligned binary coding with independent prefix) is a
 * generalization of the GUBC method described above. In GUBCIP, the first
 * header bit of each code word corresponds to J body bits, but each subsequent
 * header bit corresponds to K body bits. During compression, J and K are chosen
 * as to minimize the total space requirement.
 * GUBCIP is able to take the bimodal distribution of most positional posting
 * lists into account and achieves compression rates almost as good as Huffman.
 **/
byte * compressGUBCIP(offset *uncompressed, int listLen, int *byteLen);
offset * decompressGUBCIP(byte *compressed, int byteLen, int *listLen, offset *outBuf);

/**
 * PforDelta compression
 * See Heman et al., "Super-Scalar RAM-CPU Cache Compression", Proceedings of
 * the 22nd International Conference on Data Engineering (ICDE 2006).
 **/
byte * compressPforDelta(offset *uncompressed, int listLen, int *byteLen);
offset * decompressPforDelta(byte *compressed, int byteLen, int *listLen, offset *outBuf);

/**
 * GroupVarInt compression
 * See Jeff Dean's keynote talk at WSDM 2009:
 * http://research.google.com/people/jeff/WSDM09-keynote.pdf
 **/
byte * compressGroupVarInt(offset *uncompressed, int listLen, int *byteLen);
offset * decompressGroupVarInt(byte *compressed, int byteLen, int *listLen, offset *outBuf);


byte * compressExperimental(offset *uncompressed, int listLen, int *byteLen);
offset * decompressExperimental(byte *compressed, int byteLen, int *listLen, offset *outBuf);


byte * compressBest(offset *uncompressed, int listLen, int *byteLen);


/**
 * General decompression function that chooses the actual algorithm to employ,
 * based on the header information it finds in the input. If "outputBuffer" is
 * non-NULL, the decompressed list will be written to the given buffer. Otherwise,
 * a new buffer is allocated. The function returns a pointer to the output
 * buffer.
 **/
offset *decompressList(byte *compressed, int byteLen, int *listLen, offset *outBuf);

/** Returns the compression mode used to compressed the given list of postings. **/
int extractCompressionModeFromList(byte *compressed);


/**
 * Takes two compressed posting lists and combines them into one big list. Since we use delta
 * encoding to compress posting lists, this function needs to be given the last offset value
 * in the first list so that it can update the first delta value in the second list accordingly.
 * The "append" parameter defines whether the compressed list returned resides in a freshly
 * allocated buffer or whether it is stored in the buffer described by "firstList" (in the
 * latter case, make sure the buffer is big enough to hold both lists!).
 **/
byte *mergeCompressedLists(byte *firstList, int firstByteLen, byte *secondList, int secondByteLen,
			offset lastInFirst, int *newLen, int *newByteLen, bool append);


int getCompressorForName(const char *name);


/** Array for fast lookup of compression function, given compression mode ID. **/
static Compressor compressorForID[COMPRESSOR_COUNT] = {
	0, // invalid
	compressGamma,
	compressDelta,
	compressVByte,
	compressSimple9,
	compressInterpolative,
	compressNibble,
	compressLLRun,
	compressRice,
	compressGolomb,
	compressGUBC,
	compressGUBCIP,
	compressPforDelta,
	compressGroupVarInt,
	compressNone,
	compressLLRunMulti,
	compressHuffmanDirect,
	compressHuffman2,
	compressInterpolative_SI,
	compressRice_SI,
	compressExperimental,
	compressBest,
};


/** Array for fast lookup of decompression function, given compression mode ID. **/
static Decompressor decompressorForID[COMPRESSOR_COUNT] = {
	0, // invalid
	decompressGamma,
	decompressDelta,
	decompressVByte,
	decompressSimple9,
	decompressInterpolative,
	decompressNibble,
	decompressLLRun,
	decompressRice,
	decompressGolomb,
	decompressGUBC,
	decompressGUBCIP,
	decompressPforDelta,
	decompressGroupVarInt,
	decompressNone,
	0, // decompressLLRunMulti
	decompressHuffmanDirect,
	decompressHuffman2,
	0, // decompressInterpolative_SI
	0, // decompressRice_SI
	decompressExperimental, // decompressExperimental
	0, // decompressBest
};


#endif


