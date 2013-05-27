/**
 * Implementation of compressed cached extents.
 *
 * author: Stefan Buettcher
 * created: 2005-11-25
 * changed: 2006-01-23
 **/


#include "cached_extents.h"
#include "../index/index_compression.h"
#include "../misc/all.h"


byte *compressCachedExtentBlock(offset *start, offset *end, int count) {
	byte *result = (byte*)malloc(count * 12);
	int outPtr = 0;
	offset pointOfReference = 0;
	for (int i = 0; i < count; i++) {
		outPtr += encodeVByteOffset(start[i] - pointOfReference, &result[outPtr]);
		pointOfReference = start[i];
		outPtr += encodeVByteOffset(end[i] - pointOfReference, &result[outPtr]);
	}
	result = typed_realloc(byte, result, outPtr);
	return result;
} // end of compressCachedExtentBlock(offset*, offset*, int, CachedExtentBlock*)


void decompressCachedExtentBlock(byte *compressedData, int count, offset *start, offset *end) {
	int inPtr = 0;
	offset pointOfReference = 0;
	for (int i = 0; i < count; i++) {
		offset delta;
		inPtr += decodeVByteOffset(&delta, &compressedData[inPtr]);
		pointOfReference = start[i] = pointOfReference + delta;
		inPtr += decodeVByteOffset(&delta, &compressedData[inPtr]);
		end[i] = pointOfReference + delta;
	}
} // end of decompressCachedExtentBlock(byte*, int, offset*, offset*)


CachedExtents * createCachedExtents(offset *start, offset *end, offset count) {
	CachedExtents *result = typed_malloc(CachedExtents, 1);
	result->extentCount = count;
	result->totalSize = 0;
	result->blockCount = (count + CACHED_EXTENTS_BLOCK_SIZE - 1) / CACHED_EXTENTS_BLOCK_SIZE;
	result->compressedBlockData = typed_malloc(byte*, result->blockCount + 1);
	result->firstStart = typed_malloc(offset, result->blockCount + 1);
	result->firstEnd = typed_malloc(offset, result->blockCount + 1);
	result->lastStart = typed_malloc(offset, result->blockCount + 1);
	result->lastEnd = typed_malloc(offset, result->blockCount + 1);
	for (int i = 0; i < result->blockCount; i++) {
		int blockSize = CACHED_EXTENTS_BLOCK_SIZE;
		if (count < blockSize)
			blockSize = count;
		result->compressedBlockData[i] = compressCachedExtentBlock(start, end, blockSize);
		result->firstStart[i] = start[0];
		result->firstEnd[i] = end[0];
		result->lastStart[i] = start[blockSize - 1];
		result->lastEnd[i] = end[blockSize - 1];
		for (int k = 0; k < blockSize; k++)
			result->totalSize += (end[k] - start[k] + 1);
		start = &start[blockSize];
		end = &end[blockSize];
		count -= blockSize;
	}
	return result;
} // end of createCachedExtents(offset*, offset*, offset)


CachedExtents * createCachedExtents(ExtentList *list) {
	CachedExtents *result = typed_malloc(CachedExtents, 1);
	offset count = list->getLength();
	result->extentCount = count;
	result->totalSize = 0;
	result->blockCount = (count + CACHED_EXTENTS_BLOCK_SIZE - 1) / CACHED_EXTENTS_BLOCK_SIZE;
	result->compressedBlockData = typed_malloc(byte*, result->blockCount + 1);
	result->firstStart = typed_malloc(offset, result->blockCount + 1);
	result->firstEnd = typed_malloc(offset, result->blockCount + 1);
	result->lastStart = typed_malloc(offset, result->blockCount + 1);
	result->lastEnd = typed_malloc(offset, result->blockCount + 1);
	offset currentPosition = 0;
	for (int i = 0; i < result->blockCount; i++) {
		offset start[CACHED_EXTENTS_BLOCK_SIZE], end[CACHED_EXTENTS_BLOCK_SIZE];
		int blockSize =
			list->getNextN(currentPosition, MAX_OFFSET, CACHED_EXTENTS_BLOCK_SIZE, start, end);
		assert(blockSize > 0);
		result->compressedBlockData[i] = compressCachedExtentBlock(start, end, blockSize);
		result->firstStart[i] = start[0];
		result->firstEnd[i] = end[0];
		result->lastStart[i] = start[blockSize - 1];
		result->lastEnd[i] = end[blockSize - 1];
		for (int k = 0; k < blockSize; k++)
			result->totalSize += (end[k] - start[k] + 1);
		currentPosition = start[blockSize - 1] + 1;
	}
	return result;
} // end of createCachedExtents(ExtentList*)


void freeCachedExtents(CachedExtents *cachedExtents) {
	if (cachedExtents == NULL)
		return;
	free(cachedExtents->firstStart);
	free(cachedExtents->firstEnd);
	free(cachedExtents->lastStart);
	free(cachedExtents->lastEnd);
	for (offset i = 0; i < cachedExtents->blockCount; i++)
		free(cachedExtents->compressedBlockData[i]);
	free(cachedExtents->compressedBlockData);
	free(cachedExtents);
} // end of freeCachedExtents(CachedExtents*)



