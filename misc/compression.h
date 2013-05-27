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
 * This file defined basic lossless data compression methods that can be
 * used to realize fast in-memory data compression and decompression.
 *
 * author: Stefan Buettcher
 * created: 2005-05-28
 * changed: 2005-05-28
 **/


#ifndef __MISC__COMPRESSION_H
#define __MISC__COMPRESSION_H


static const int INITIAL_LZW_BITLENGTH  = 9;

static const int MAX_LZW_BITLENGTH = 12;

static const int MAX_LZW_TABLE_SIZE = (1 << MAX_LZW_BITLENGTH);

static const int MAX_PTR_BACKWARDS = 128;

static const int PTR_STOP_SEARCH = 32;


typedef unsigned char byte;


/**
 * Compresses the data found in "uncompressed" and puts them into the buffer
 * provided by "compressed" of size "maxSize". The actual size of the compressed
 * data will be stored in "size". -1 if there is not enough space.
 **/
void compressLZW(byte *uncompressed, byte *compressed, int inSize, int *outSize, int maxOutSize);

/**
 * Decompresses the data found in "compressed" and puts them into the buffer
 * provided by "uncompressed" of size "maxSize". The actual size of the
 * uncompressed data will be stored in "size". -1 if there is not enough space.
 **/
void decompressLZW(byte *compressed, byte *uncompressed, int *outSize, int maxOutSize);

void compressPTR(byte *uncompressed, byte *compressed, int inSize, int *outSize, int maxOutSize);

void decompressPTR(byte *compressed, byte *uncompressed, int *outSize, int maxOutSize);


#endif


