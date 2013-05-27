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
 * SegmentedPostingList is a variant of PostingList in which the postings are
 * kept in memory or on disk, in compressed forms, stored in segments. Whenever
 * a segment is needed, it is loaded into memory and decompressed, possibly
 * evicting another segment from memory. SegmentedPostingList is necessary
 * because we only have a very limited amount of memory available for query
 * processing.
 *
 * author: Stefan Buettcher
 * created: 2004-11-09
 * changed: 2006-04-24
 **/


#ifndef __INDEX__SEGMENTEDPOSTINGLIST_H
#define __INDEX__SEGMENTEDPOSTINGLIST_H


#include "../extentlist/extentlist.h"
#include "../filesystem/filesystem.h"


typedef struct {

	/** This is the file that contains the postings. **/
	File *file;

	/** Number of postings in this segment. **/
	int32_t count;

	/** Number of bytes occupied by compressed postings. **/
	int32_t byteLength;

	/** Index address values of first and last posting in the segment. **/
	offset firstPosting, lastPosting;
	
} SPL_OnDiskSegment;


typedef struct {

	/** Byte array, containing compressed postings. **/
	byte *postings;

	/** Number of bytes occupied by the compressed buffer. **/
	int byteLength;

	/** Number of postings in this segment. **/
	int count;

	/** Index address values of first and last posting in the segment. **/
	offset firstPosting, lastPosting;

	/** Unique ID number of the segment referred to by this descriptor. **/
	int segmentID;

	/** Time stamp used by the LRU cache strategy. **/
	int timeStamp;
	
} SPL_InMemorySegment;


typedef struct {

	/** Array of decompressed postings. **/
	offset *postings;

	/** Number of postings in this segment. **/
	int count;

	/** Unique ID number of the segment referred to by this descriptor. **/
	int segmentID;

	/** Time stamp used by the LRU cache strategy. **/
	int timeStamp;

} SPL_DecompressedSegment;


class SegmentedPostingList : public ExtentList {

	friend class CompactIndex;
	friend class Optimizer;
	friend class Simplifier;
	friend class TerabyteQuery;
	friend void *decompressListConcurrently(void *data);
	friend void processQueries();

public:

	/** Number of decompressed segments we can hold in memory at the same time. **/
	static const int DECOMPRESSED_SEGMENT_COUNT = 2;

	/** Number of compressed segments we can hold in memory at the same time. **/
	static const int IN_MEMORY_SEGMENT_COUNT = 64;

	/**
	 * This number defines how many segments we read into memory when we detect
	 * a sequential access pattern inside the loadSegment(int) method.
	 **/
	static const int READ_AHEAD_SEGMENT_COUNT = 60;

protected:

	/** First-level cache, containing decompressed postings. **/
	SPL_DecompressedSegment decompressedSegments[DECOMPRESSED_SEGMENT_COUNT];

	/** Second-level cache, containing compressed postings. **/
	SPL_InMemorySegment compressedSegments[IN_MEMORY_SEGMENT_COUNT];

	/** Tertiary storage system (compressed in-memory segments). **/
	SPL_InMemorySegment *inMemorySegments;

	/** Tertiary storage system (on-disk segments). **/
	SPL_OnDiskSegment *onDiskSegments;

	/** Total number of segments in this list (size of the "onDiskSegments" array). **/
	int segmentCount;

	/** First and last posting in this list. **/
	offset firstPosting, lastPosting;

	/** Used for updating the cache slots' time stamps (LRU-style caching). **/
	int currentTimeStamp;

	/** Uncompressed postings in currently accessed segment. **/
	offset *currentSegment;

	/** ID number of the currently accessed segment. **/
	int currentSegmentID;

	/** Number of postings in current segment. **/
	int currentSegmentLength;

	/** First and last posting in current segment. **/
	offset currentFirst, currentLast;

	/** Current position (array offset) in the current segment. **/
	int currentPosition;

	/** Total number of postings in the list. **/
	offset totalLength;

	/**
	 * Tells us whether we have to release the memory occupied by the compressed
	 * postings in the destructor.
	 **/
	bool mustFreeCompressedBuffers;

private:

	/** Tells us whether the object has already been fully initialized or not. **/
	bool initialized;

	char errorMessage[256];

public:

	/**
	 * Creates a new SegmentedPostingList instance that reads its data from files.
	 * Segments do not have to come in in ascending order. They are automatically
	 * sorted by the constructor.
	 **/
	SegmentedPostingList(SPL_OnDiskSegment *segments, int segmentCount);

	/**
	 * Creates a new SegmentedPostingList instance that reads its data from
	 * compressed in-memory buffers. "mustFreeSegments" tells us whether we have
	 * to free the memory occupied by the compressed postings in the destructor.
	 * Segments do not have to come in in ascending order. They are automatically
	 * sorted by the constructor.
	 **/
	SegmentedPostingList(SPL_InMemorySegment *segments, int segmentCount,
			bool mustFreeCompressedBuffers);

	/**
	 * Frees all memory occupied by the object. This includes memory referenced by
	 * any of the parameters passed to the class constructor. If the object reads
	 * its data from a files, the files are closed. Call "unlink" before if you want
	 * be make sure the files are deleted after the object is destroyed.
	 **/
	virtual ~SegmentedPostingList();

public:

	bool getFirstStartBiggerEq(offset position, offset *start, offset *end);

	bool getFirstEndBiggerEq(offset position, offset *start, offset *end);

	bool getLastStartSmallerEq(offset position, offset *start, offset *end);

	bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	int getNextN(offset from, offset to, int n, offset *start, offset *end);

	bool getNth(offset b, offset *start, offset *end);

	/**
	 * Returns a pointer to an array of N postings, where the value of N can be
	 * obtained by calling getLength(). The postings resulted from uncompressing
	 * this posting list. Memory has to be freed by the caller.
	 **/
	offset *toArray();
	
	byte **getSegments(int *segmentCount);

	offset getLength();

	offset getCount(offset start, offset end);

	long getMemoryConsumption();

	bool isSecure();

	bool isAlmostSecure();

	ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	char *toString();

	int getType();

	/**
	 * Initializes the internal data structures of the object. This is done from
	 * within loadSegmentInToCache(int) when the method is first called.
	 **/
	void initialize();

private:

	/**
	 * Makes the first segment containing a posting >= position the currently
	 * active segment.
	 **/
	void loadFirstSegmentBiggerEq(offset position);

	/**
	 * Makes the last segment containing a posting <= position the currently
	 * active segment.
	 **/
	void loadLastSegmentSmallerEq(offset position);

	/**
	 * Loads the given segment into the first-level cache and makes it the currently
	 * active segment.
	 **/
	void loadSegment(int id);

	/**
	 * Loads the segment with ID "id" into the second-level cache. If there are no
	 * free slots in the cache, another segment will be evicted (LRU). Returns the
	 * slot that the loaded segment resides in.
	 **/
	int loadSegmentIntoL2(int id);

	/**
	 * Tells us whether the segment with the given ID is currently in the L2 cache.
	 **/
	bool isSegmentInL2(int id);

}; // end of class SegmentedPostingList


#endif


