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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../index/index_compression.h"


int main(int argc, char **argv) {
	assert(argc == 2);
	int N = atoi(argv[1]);
	offset *first = new offset[N];
	offset *second = new offset[N];
	offset current = 23;
	for (int i = 0; i < N; i++) {
		current += random() % 29176;
		first[i] = current;
	}
	for (int i = 0; i < N; i++) {
		current += random() % 31085;
		second[i] = current;
	}
	int fbl, sbl;
	byte *fc = compressByteBased(first, N, &fbl);
	byte *sc = compressByteBased(second, N, &sbl);

	int length, byteLength;
	byte *combined = mergeCompressedLists(fc, fbl, sc, sbl, first[N - 1], &length, &byteLength);
	assert(length == 2 * N);

	int fromDecompressor;
	offset *decompressed = decompressList(combined, byteLength, &fromDecompressor, NULL);
	assert(fromDecompressor == length);

	printf("length = %d, byteLength = %d\n", length, byteLength);

	for (int i = 0; i < N; i++)
		assert(decompressed[i] == first[i]);
	for (int i = 0; i < N; i++)
		assert(decompressed[i + N] == second[i]);
	
	return 0;
} // end of main()


