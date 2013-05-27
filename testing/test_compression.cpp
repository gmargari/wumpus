/**
 * author: Stefan Buettcher
 * created: 2007-09-06
 * changed: 2007-09-06
 **/


#include <stdio.h>
#include <stdlib.h>
#include "testing.h"
#include "../index/index_compression.h"
#include "../index/index_types.h"
#include "../misc/alloc.h"


void TESTCASE_BasicVByte(int *passed, int *failed) {
	byte buffer[16];
	*passed = 1;
	*failed = 0;
	for (int32_t i = 0; i < 1000000000; i += (random() % (i + 1)) + 1) {
		encodeVByte32(i, buffer);
		int32_t value;
		decodeVByte32(&value, buffer);
		if (value != i) {
			*passed--;
			*failed++;
			break;
		}
	}
	if (sizeof(offset) > 4) {
		*passed++;
		for (offset i = 1000000000; i < 1E17; i += i + random() % i) {
			encodeVByteOffset(i, buffer);
			offset value;
			decodeVByteOffset(&value, buffer);
			if (value != i) {
				*passed--;
				*failed++;
				break;
			}
		}
	}
} // end of TESTCASE_BasicVByte(int*, int*)


void TESTCASE_PostingsCompression(int *passed, int *failed) {
	*passed = *failed = 0;

	for (int len = 1; len < 100000; len += (random() % len) + 1) {
		for (int avg = 1; avg <= 1024; avg *= 2) {
			offset *list = typed_malloc(offset, len);
			offset prev = -1;
			for (int i = 0; i < len; i++) {
				prev += random() % (avg * 2 - 1) + 1;
				list[i] = prev;
			}
			for (int method = 0; method < COMPRESSOR_COUNT; method++) {
				if (((method < START_OF_SIMPLE_COMPRESSORS) || (method > END_OF_SIMPLE_COMPRESSORS)) &&
						(method != COMPRESSION_EXPERIMENTAL)) {
					continue;
				}
				int byteLen, listLen;
				byte *compressed = compressorForID[method](list, len, &byteLen);
				offset *uncompressed = decompressList(compressed, byteLen, &listLen, NULL);
				if (len != listLen) {
					fprintf(stderr, "len != listLen for method %d.\n", method);
					*failed = 1;
					return;
				}
				for (int i = 0; i < len; i++)
					if (list[i] != uncompressed[i]) {
						fprintf(stderr,
						        "Incorrect decompression result for method %d: %lld != %lld "
										"(listLen=%d, position=%d).\n",
										method, (long long)uncompressed[i], (long long)list[i], listLen, i);
						*failed = 1;
						return;
					}
				free(compressed);
				free(uncompressed);
			}
			free(list);
		}
	}
	*passed = *passed + 1;
	if (sizeof(offset) <= 4)
		return;

	for (int len = 1000; len < 10000; len += random() % len + 1) {
		for (offset avg = 1000; avg < 1E11; avg += random() % avg + 1) {
			offset *list = typed_malloc(offset, len);
			offset prev = -1;
			for (int i = 0; i < len; i++) {
				offset r = random() % 1000000000;
				r = r * 1000000000 + random() % 1000000000;
				prev += random() % (avg * 2 - 1) + 1;
				list[i] = prev;
			}
			for (int method = START_OF_SIMPLE_COMPRESSORS; method <= END_OF_SIMPLE_COMPRESSORS; method++) {
				int byteLen, listLen;
				byte *compressed = compressorForID[method](list, len, &byteLen);
				offset *uncompressed = decompressList(compressed, byteLen, &listLen, NULL);
				if (len != listLen) {
					*failed = 1;
					return;
				}
				for (int i = 0; i < len; i++)
					if (list[i] != uncompressed[i]) {
						fprintf(stderr, "Incorrect decompression result for method %d.\n", method);
						*failed = 1;
						return;
					}
				free(compressed);
				free(uncompressed);
			}
			free(list);
		}
	}
	*passed = *passed + 1;
} // end of TESTCASE_PostingsCompression(int*, int*)



