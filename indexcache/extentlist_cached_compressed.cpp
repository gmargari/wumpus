/**
 * Implementation of the ExtentList_Cached_Compressed class. See the header file
 * for documentation.
 *
 * author: Stefan Buettcher
 * created: 2006-01-18
 * changed: 2006-08-27
 **/


#include <assert.h>
#include <string.h>
#include "extentlist_cached_compressed.h"
#include "indexcache.h"
#include "../index/postinglist.h"
#include "../misc/all.h"


ExtentList_Cached_Compressed::ExtentList_Cached_Compressed(IndexCache *cache, int cacheID, CachedExtents *extents) {
	this->cache = cache;
	this->cacheID = cacheID;
	length = count = extents->extentCount;
	assert(count > 0);
	blockCount = (count + CACHED_EXTENTS_BLOCK_SIZE - 1) / CACHED_EXTENTS_BLOCK_SIZE;
	assert(blockCount == extents->blockCount);
	almostSecure = true;

	cachedExtents = extents;
	firstStartInBlock = extents->firstStart;
	firstEndInBlock = extents->firstEnd;
	lastStartInBlock = extents->lastStart;
	lastEndInBlock = extents->lastEnd;
	compressedBlockData = extents->compressedBlockData;

	currentBlock = -1;
	loadBlock(0);
} // end of ExtentList_Cached_Compressed(IndexCache*, int, CachedExtents*)


ExtentList_Cached_Compressed::~ExtentList_Cached_Compressed() {
	if (cache != NULL)
		cache->deregister(cacheID);
	else
		freeCachedExtents(cachedExtents);
} // end of ~ExtentList_Cached_Compressed()


bool ExtentList_Cached_Compressed::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	if ((position < this->start[0]) || (position > this->start[currentBlockSize - 1])) {
		int newBlock = findFirstPostingBiggerEq(position, lastStartInBlock, blockCount, currentBlock);
		if (newBlock < 0)
			return false;
		loadBlock(newBlock);
	}
	currentBlockPos = findFirstPostingBiggerEq(position, this->start, currentBlockSize, currentBlockPos);
	*start = this->start[currentBlockPos];
	*end = this->end[currentBlockPos];
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Cached_Compressed::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if ((position < this->end[0]) || (position > this->end[currentBlockSize - 1])) {
		int newBlock = findFirstPostingBiggerEq(position, lastEndInBlock, blockCount, currentBlock);
		if (newBlock < 0)
			return false;
		loadBlock(newBlock);
	}
	currentBlockPos = findFirstPostingBiggerEq(position, this->end, currentBlockSize, currentBlockPos);
	*start = this->start[currentBlockPos];
	*end = this->end[currentBlockPos];
	return true;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Cached_Compressed::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if ((position < this->start[0]) || (position > this->start[currentBlockSize - 1])) {
		int newBlock = findLastPostingSmallerEq(position, firstStartInBlock, blockCount, currentBlock);
		if (newBlock < 0)
			return false;
		loadBlock(newBlock);
	}
	currentBlockPos = findLastPostingSmallerEq(position, this->start, currentBlockSize, currentBlockPos);
	*start = this->start[currentBlockPos];
	*end = this->end[currentBlockPos];
	return true;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Cached_Compressed::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	if ((position < this->start[0]) || (position > this->start[currentBlockSize - 1])) {
		int newBlock = findLastPostingSmallerEq(position, firstEndInBlock, blockCount, currentBlock);
		if (newBlock < 0)
			return false;
		loadBlock(newBlock);
	}
	currentBlockPos = findLastPostingSmallerEq(position, this->end, currentBlockSize, currentBlockPos);
	*start = this->start[currentBlockPos];
	*end = this->end[currentBlockPos];
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


int ExtentList_Cached_Compressed::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	offset s, e;
	int result = 0;
	while (result < n) {
		if (!ExtentList_Cached_Compressed::getFirstStartBiggerEq(from, &s, &e))
			break;
		if (e > to)
			break;
		while (this->end[currentBlockPos] <= to) {
			start[result] = this->start[currentBlockPos];
			end[result] = this->end[currentBlockPos];
			if (++result >= n)
				break;
			if (++currentBlockPos >= currentBlockSize)
				break;
		}
		if (currentBlockPos >= currentBlockSize)
			currentBlockPos = currentBlockSize - 1;
		if (result > 0)
			from = start[result - 1] + 1;
		else
			break;
	}
	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)


offset ExtentList_Cached_Compressed::getLength() {
	return count;
} // end of getLength()


offset ExtentList_Cached_Compressed::getCount(offset start, offset end) {
	offset s, e;
	offset first, last;
	if (!ExtentList_Cached_Compressed::getFirstStartBiggerEq(start, &s, &e))
		return false;
	first = currentBlock * CACHED_EXTENTS_BLOCK_SIZE + currentBlockPos;
	if (!ExtentList_Cached_Compressed::getLastEndSmallerEq(end, &s, &e))
		return false;
	last = currentBlock * CACHED_EXTENTS_BLOCK_SIZE + currentBlockPos;
	return last - first + 1;
} // end of getCount(offset, offset)


bool ExtentList_Cached_Compressed::getNth(offset n, offset *start, offset *end) {
	if ((n < 0) || (n >= count))
		return false;
	int block = n / CACHED_EXTENTS_BLOCK_SIZE;
	int ext = n % CACHED_EXTENTS_BLOCK_SIZE;
	if (block != currentBlock)
		loadBlock(block);
	*start = this->start[ext];
	*end = this->end[ext];
	return true;
} // end of getNth(offset, offset*, offset*)


void ExtentList_Cached_Compressed::loadBlock(int whichBlock) {
	assert((whichBlock >= 0) && (whichBlock < blockCount));
	if (whichBlock == currentBlock)
		return;
	int blockSize;
	if (whichBlock < blockCount - 1)
		blockSize = CACHED_EXTENTS_BLOCK_SIZE;
	else {
		blockSize = count % CACHED_EXTENTS_BLOCK_SIZE;
		if (blockSize == 0)
			blockSize = CACHED_EXTENTS_BLOCK_SIZE;
	}
	decompressCachedExtentBlock(compressedBlockData[whichBlock], blockSize, start, end);
	currentBlock = whichBlock;
	currentBlockSize = blockSize;
	currentBlockPos = 0;
} // end of loadBlock(int)


bool ExtentList_Cached_Compressed::isAlmostSecure() {
	return almostSecure;
}


void ExtentList_Cached_Compressed::setAlmostSecure(bool value) {
	almostSecure = value;
}


char * ExtentList_Cached_Compressed::toString() {
	return duplicateString("(CACHED_COMPRESSED)");
}


int ExtentList_Cached_Compressed::getType() {
	return TYPE_EXTENTLIST_CACHED;
}


int ExtentList_Cached_Compressed::getInternalPosition() {
	if (currentBlock < 0)
		return -1;
	else
		return currentBlock * CACHED_EXTENTS_BLOCK_SIZE + currentBlockPos;
} // end of getInternalPosition()


