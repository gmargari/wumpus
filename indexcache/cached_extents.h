/**
 * author: Stefan Buettcher
 * created: 2005-11-25
 * changed: 2006-01-23
 **/


#ifndef __INDEXCACHE__CACHED_EXTENTS_H
#define __INDEXCACHE__CACHED_EXTENTS_H


#include "../extentlist/extentlist.h"
#include "../index/index_types.h"


static const int CACHED_EXTENTS_BLOCK_SIZE = 128;


typedef struct {

	/** Number of extents in this list. **/
	offset extentCount;

	/** Their total size (used to speedup @count[size] queries and such. **/
	offset totalSize;

	/** Number of blocks. **/
	offset blockCount;

	/** Start and end of first extent in each block. **/
	offset *firstStart, *firstEnd;

	/** Start and end of last extent in each block. **/
	offset *lastStart, *lastEnd;

	/** Blocks of cached extents used by this struct. **/
	byte **compressedBlockData;

} CachedExtents;


/**
 * Compresses the extents found in "start" and "end" and puts the resulting
 * compressed list into the buffer returned. Memory is allocated automatically.
 **/
byte *compressCachedExtentBlock(offset *start, offset *end, int count);

/**
 * Decompresses the extent list found inside the given block into the buffers
 * given by "start" and "end".
 **/
void decompressCachedExtentBlock(byte *compressedData, int count, offset *start, offset *end);

CachedExtents *createCachedExtents(offset *start, offset *end, offset count);

CachedExtents *createCachedExtents(ExtentList *list);

void freeCachedExtents(CachedExtents *cachedExtents);


#endif


