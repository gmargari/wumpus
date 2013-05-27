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
 * Definition of the MyInPlaceIndex class. MyInPlaceIndex represents an
 * in-place updatable index with non-contiguous posting lists.
 *
 * author: Stefan Buettcher
 * created: 2005-11-20
 * changed: 2007-08-06
 **/


#ifndef __INDEX__MY_INPLACE_INDEX_H
#define __INDEX__MY_INPLACE_INDEX_H


#include "index_types.h"
#include "compactindex.h"
#include "inplace_index.h"
#include "../extentlist/extentlist.h"


struct MyInPlaceSegmentHeader {

	/** Where is this segment in the index file? **/
	offset filePosition;

	/** How many postings does it contain? **/
	int32_t postingCount;

	/** What is its total compressed size? **/
	int32_t size;

	/** First posting in the segment. **/
	offset firstPosting;

	/** Last posting in the segment. **/
	offset lastPosting;

	/** Compressed list segment. Not normally used. Only when buffering incoming postings. **/
	byte *compressedPostings;

}; // end of struct MyInPlaceSegmentHeader


struct MyInPlaceTermDescriptor {

	/** Number of on-disk posting segments for the term. **/
	int segmentCount;

	/**
	 * A sequence of "segmentCount" compressed MyInPlaceSegmentHeaders.
	 * Compressed via "compressSegmentHeaders".
	 **/
	byte *compressedSegments;

	/** Number of bytes allocated for "compressedSegments". **/
	int allocated;

	/** Total number of postings for this guy. **/
	int64_t postingCount;

	/** Start of the index block reserved for this term. **/
	int64_t indexBlockStart;

	/** How much total space do we have in this block? (in bytes) **/
	int64_t indexBlockLength;

	/** How much of the reserved block have we actually used so far? **/
	int64_t indexBlockUsed;

}; // end of struct MyInPlaceTermDescriptor



class MyInPlaceIndex : public InPlaceIndex {

	friend class MyInPlaceIndexIterator;
	friend class OnDiskIndexManager;

public:

	/** Number of bytes reserved for each term's meta-data, initially. **/
	static const int INIT_SEGMENTS_BUFFER_SIZE = 256;

	/**
	 * Whenever we run out of memory for a term's meta-data, we realloc space.
	 * This is the growth rate for the array.
	 **/
	static const double SEGMENTS_BUFFER_GROWTH_RATE = 1.21;

protected:

	/** Path to the data file. **/
	char *fileName;

	/** Handle to the data file. **/
	int fileHandle;

	/** File object used to access the data in the index through SegmentedPostingList. **/
	FileFile *file;

	/** Total number of postings in this index. **/
	int64_t postingCount;

	/** Total number of index blocks in this index. **/
	int blockCount;

	/** Total number of bytes used for postings data. **/
	int64_t bytesUsed;

	/** Number of list updates and relocations performed in this session. **/
	unsigned int listUpdateCount, relocationCount;

	/**
	 * A sequence of "blockCount" bytes, indicating whether a given block is
	 * empty or not.
	 **/
	byte *freeMap;

	static const int MAX_PENDING_SEGMENT_COUNT = 64;

	/**
	 * A sequence of pending updates, buffered in memory as long as we are still
	 * adding postings for the same term. The purpose of this is to minimize the
	 * probability that we write postings to disk, then run out of disk space,
	 * and have to relocate the entire list. Instead, we should relocate the list
	 * before writing any new postings to disk (=> buffering postings in memory).
	 **/
	MyInPlaceSegmentHeader pendingSegments[MAX_PENDING_SEGMENT_COUNT];

	/** Number of list segments in the current buffer. **/
	int pendingSegmentCount;

	/** Maximum amount of postings data in pending segments. **/
	static const int MAX_PENDING_DATA = 4 * 1024 * 1024;

	/**
	 * The pending data themselves. The "compressedPostings" pointers in the
	 * "pendingSegments" array are pointers into this buffer.
	 **/
	byte *pendingBuffer;

	/** Number of bytes in the "pendingBuffer". **/
	int pendingData;

	/** Term that is currently being updated. **/
	char currentTerm[MAX_TOKEN_LENGTH * 2];

	/**
	 * Indicates whether we want to keep each posting list in a contiguous region
	 * of the index.
	 **/
	bool contiguous;

public:

	MyInPlaceIndex(Index *owner, const char *fileName);

	~MyInPlaceIndex();

	virtual void addPostings(const char *term, offset *postings, int count);

	virtual void addPostings(const char *term,
			byte *compressedPostings, int size, int count, offset first, offset last);

	virtual ExtentList *getPostings(const char *term);

	virtual int64_t getTermCount();

	virtual int64_t getByteSize();

	virtual int64_t getPostingCount();

	/**
	 * Returns a copy of the CompactIndex's filename. Memory has to be freed
	 * by the caller.
	 **/
	virtual char *getFileName();

	virtual void finishUpdate();

private:

	void addPostings(InPlaceTermDescriptor *term,
			byte *compressedPostings, int byteLength, int count, offset first, offset last);

	void compressSegmentHeaders(
			MyInPlaceSegmentHeader *headers, int count, byte *output, int *size);

	void decompressSegmentHeaders(
			byte *compressed, int count, int size, MyInPlaceSegmentHeader *output);

	/** Writes all pending updates to disk. **/
	void flushPendingData();

	/**
	 * Returns a pointer to the term descriptor for the given term. If no such
	 * descriptor can be found, a new one is created and a pointer is returned.
	 **/
	MyInPlaceTermDescriptor *getDescriptorOrCreate(const char *term, int64_t spaceNeeded);

	/** Creates a new descriptor for a term occupying a single index block. **/
	MyInPlaceTermDescriptor *createNewDescriptor(int64_t spaceNeeded);

	/** The following three methods are for free space management. **/
	int allocateBlocks(int count);
	void freeBlocks(int start, int count);
	void markBlocks(int start, int count, char value);

	/**
	 * Relocates the given posting list to a new place, where there is enough space
	 * for the new postings.
	 **/
	void relocatePostings(MyInPlaceTermDescriptor *desc, int64_t spaceNeeded);

	/**
	 * Similar to "relocatePostings", but does not relocate the existing postings,
	 * but allocates a completely new chunk somewhere else.
	 **/
	void allocateViaChaining(MyInPlaceTermDescriptor *desc, int64_t spaceNeeded);

	void printSummary();

}; // end of class MyInPlaceIndex


#endif


