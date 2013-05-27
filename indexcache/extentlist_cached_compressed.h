/**
 * Definition of the ExtentList_Cached_Compressed class. ExtentList_Cached_Compressed
 * adds compression capabilities to the existing caching facilities.
 *
 * Uses CachedExtents to access compressed blocks of CACHED_EXTENTS_BLOCK_SIZE extents
 * each.
 *
 * author: Stefan Buettcher
 * created: 2006-01-18
 * changed: 2006-01-20
 **/


#ifndef __INDEX_TOOLS__EXTENTLIST_CACHED_COMPRESSED_H
#define __INDEX_TOOLS__EXTENTLIST_CACHED_COMPRESSED_H


#include "cached_extents.h"


class IndexCache;


class ExtentList_Cached_Compressed : public ExtentList {

private:

	/** The IndexCache instance that gave us the data. **/
	IndexCache *cache;

	int cacheID;

	/** Number of extents managed by this instance. **/
	int count, blockCount;

	/** Pointer to compressed extents for each block managed by this object. **/
	byte **compressedBlockData;

	/**
	 * For easier access into the list of blocks (to find the right one for a
	 * getFirstStartBiggerEq operation etc.
	 **/
	offset *firstStartInBlock, *firstEndInBlock, *lastStartInBlock, *lastEndInBlock;	

	/** The data we are working on. **/
	CachedExtents *cachedExtents;

	/** Current block, in uncompressed form. **/
	offset start[CACHED_EXTENTS_BLOCK_SIZE], end[CACHED_EXTENTS_BLOCK_SIZE];

	/** Some meta-information about the current block. **/
	int currentBlock, currentBlockSize, currentBlockPos;

	/** For security crap. Most probably useless in this context. **/
	bool almostSecure;

public:

	/**
	 * Creates a new ExtentList from the data found in "extents". Depending on whether
	 * "cache" is NULL or not, it will leave control over "extents" with the caller
	 * (the IndexCache instance associated with the data) or take control over everything
	 * referenced by "extents". This includes freeing the memory at will.
	 **/
	ExtentList_Cached_Compressed(IndexCache *cache, int cacheID, CachedExtents *extents);

	~ExtentList_Cached_Compressed();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	virtual bool getNth(offset n, offset *start, offset *end);

	virtual bool isAlmostSecure();

	virtual char *toString();

	virtual int getInternalPosition();

	virtual int getType();

	void setAlmostSecure(bool value);

private:

	/**
	 * Decompresses the given block and makes it the currrent block for all subsequent
	 * search operations.
	 **/
	void loadBlock(int whichBlock);

}; // end of class ExtentList_Cached_Compressed


#endif


