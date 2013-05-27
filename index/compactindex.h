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
 * Definition of the CompactIndex class. CompactIndex is essentially a sequence of
 * (term, postings) pairs.
 *
 * author: Stefan Buettcher
 * created: 2005-01-07
 * changed: 2008-08-14
 **/


#ifndef __INDEX__COMPACTINDEX_H
#define __INDEX__COMPACTINDEX_H


#include "index_types.h"
#include "index_compression.h"
#include "ondisk_index.h"
#include "../config/config.h"
#include "../filesystem/filefile.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"


class Index;
class IndexIterator;


/** Header information for long list segments. **/
struct PostingListSegmentHeader {

	int32_t postingCount;

	int32_t byteLength;

	offset firstElement;

	offset lastElement;

}; // end of struct PostingListSegmentHeader


struct CompactIndex_BlockDescriptor {

	/** The first term in this index block. **/
	char firstTerm[MAX_TOKEN_LENGTH + 1];

	/** File position of block start. **/
	off_t blockStart;

	/** File position of block end (start of next block). **/
	off_t blockEnd;

}; // end of struct CompactIndex_BlockDescriptor


/**
 * Header information for on-disk CompactIndex instances. Not quite a header, since
 * it is found at the end of the file.
 **/
struct CompactIndex_Header {

	/** Number of terms in the index. **/
	uint32_t termCount;

	/** Number of list segments. **/
	uint32_t listCount;

	/** Number of internal nodes in the 2-level "B-tree". **/
	uint32_t descriptorCount;

	/** Total number of postings in the index. **/
	offset postingCount;

}; // end of struct CompactIndex_Header


class CompactIndex : public OnDiskIndex {

	friend class TerabyteQuery;

public:

	/** Size of the output buffer for index creating and merging. **/
	static const int WRITE_CACHE_SIZE = 4 * 1024 * 1024;

	/**
	 * Maximum number of posting list segments to hold in memory during
	 * index construction.
	 **/
	static const int MAX_SEGMENTS_IN_MEMORY = WRITE_CACHE_SIZE / TARGET_SEGMENT_SIZE;

	/**
	 * When we merge indices, we recommend this buffer size for the individual
	 * read buffers.
	 **/
	static const int DEFAULT_MERGE_BUFFER_PER_INDEX = 1024 * 1024;

	static const double DESCRIPTOR_GROWTH_RATE = 1.21;

	/** Compression function for the compression mode that is specified in config.h. **/
	Compressor compressor;

protected:

	CompactIndex_Header header;

	/** Index instance that controls us. **/
	Index *owner;

	/** Name of the index file. **/
	char *fileName;

	/** File handle of the index file. **/
	int fileHandle;

	/**
	 * The compression method to use when compressing posting lists.
	 * Initialized to INDEX_COMPRESSION_MODE in the constructor.
	 **/
	int indexCompressionMode;

	/** Keeping track of free memory for interval descriptors. **/
	int descriptorSlotCount;

	/** The descriptors themselves. **/
	CompactIndex_BlockDescriptor *descriptors;

	/** File position of current (i.e., last) index block. **/
	int64_t startPosOfLastBlock;

	/** Data write cache. **/
	byte *writeCache;

	/** Number of bytes in the cache that are used. **/
	int cacheBytesUsed;

	/** Number of bytes written to file so far. **/
	off_t bytesWrittenToFile;

	/** Used to verify that the resulting term sequence is sorted. **/
	char lastTermAdded[MAX_TOKEN_LENGTH + 1];

	/**
	 * We basically have two operation modes here: If a CompactIndex is read-only,
	 * wecan search it, but we can't append postings. If it is not read-only, we
	 * can append postings, but we cannot search it.
	 **/
	bool readOnly;

	/**
	 * Indicates whether file access takes place directly or through the file
	 * system cache. Only used during index creation, not for querying.
	 **/
	bool use_O_DIRECT;

	/**
	 * In order to be able to have thousands of SegmentedPostingList instances
	 * reading from this CompactIndex at the same time, we need to implement a
	 * virtual file layer. Every File object used by a SegmentedPostingList does
	 * in fact sit on top of this FileFile object and read/write through it.
	 **/
	FileFile *baseFile;

	/**
	 * Contains a copy of all index data, in case the index has been loaded
	 * into memory (configuration variable ALL_INDICES_IN_MEMORY).
	 **/
	char *inMemoryIndex;

	/** Total index size, in bytes. Only set when loaded into RAM. **/
	int64_t totalSize;

	PostingListSegmentHeader tempSegmentHeaders[MAX_SEGMENTS_IN_MEMORY];
	byte *tempSegmentData[MAX_SEGMENTS_IN_MEMORY];
	int32_t tempSegmentCount;
	long totalSizeOfTempSegments;

protected:

	CompactIndex();

	/**
	 * Creates a new CompactIndex instance managing an existing (or to-be-created)
	 * on-disk inverted file.
	 **/
	CompactIndex(Index *owner, const char *fileName, bool create, bool use_O_DIRECT);

	/** Creates a new instance by loading an on-disk inverted file into RAM. **/
	CompactIndex(Index *owner, const char *fileName);

	/** Sets up the data structures necessary for query processing. **/
	virtual void initializeForQuerying();

	/** Reads the entire on-disk index into an in-memory buffer ("inMemoryIndex"). **/
	virtual void loadIndexIntoMemory();

public:

	/**
	 * Returns a CompactIndex instance for an index created (or re-opened) with
	 * the given parameters. Users of CompactIndex need to use this factory method
	 * instead of the class constructor, because we also have a CompactIndex2 class
	 * that is used whenever the factory method detects that the on-disk index
	 * is in the new format.
	 **/
	static CompactIndex *getIndex(
			Index *owner, const char *fileName, bool create, bool use_O_DIRECT = false);

	/**
	 * Returns an IndexIterator (or IndexIterator2) instance for the on-disk
	 * index stored in the given file. Uses a read buffer of "bufferSize" bytes.
	 **/
	static IndexIterator *getIterator(const char *fileName, int bufferSize);

	virtual ~CompactIndex();

	virtual void setIndexCompressionMode(int indexCompressionMode) {
		this->indexCompressionMode = indexCompressionMode;
	}

	virtual int getIndexCompressionMode() {
		return this->indexCompressionMode;
	}

	/** Writes all pending data to disk. **/
	virtual void flushWriteCache();

	/** Similar to flushWriteCache, but only writes multiples of 64 KB. **/
	virtual void flushPartialWriteCache();

	/**
	 * Adds a new term with the given postings to the index. Lexicographical ordering must
	 * be preserved, i.e. "X" must come before "Y".
	 **/
	virtual void addPostings(const char *term, offset *postings, int count);

	/** Same as above, but with a compressed postings list. **/
	virtual void addPostings(const char *term, byte *compressedPostings,
			int byteLength, int count, offset first, offset last);

	/**
	 * Returns an ExtentList instance that contains all postings for the term given
	 * by "term". If the term cannot be found in the index, an ExtentList_Empty instance
	 * is returned. Wildcard terms, such as "$effective" and "europ*", are permitted.
	 **/
	virtual ExtentList *getPostings(const char *term);

	/**
	 * Returns a copy of the CompactIndex's filename. Memory has to be freed
	 * by the caller.
	 **/
	char *getFileName();

	/** Returns how many terms are in this CompactIndex. **/
	virtual int64_t getTermCount();

	/** Returns the total size of this CompactIndex instance in bytes. **/
	virtual int64_t getByteSize();

	virtual int64_t getPostingCount();

	/**
	 * Compresses the contents of "header" into the given buffer. Returns the
	 * number of bytes consumed by the compressed representation.
	 **/
	static int compressPLSH(
			const PostingListSegmentHeader *header, offset referencePosting, byte *buffer);

	/**
	 * Counterpart to compressPLSH. Also returns the number of bytes occupied
	 * by the compressed representation.
	 **/
	static int decompressPLSH(
			const byte *buffer, offset referencePosting, PostingListSegmentHeader *header);

protected:

	virtual void copySegmentsToWriteCache();

	virtual void addDescriptor(const char *term);

	/** Returns a File object that represents the data stored in the index. **/
	virtual FileFile *getFile();

	/**
	 * This method is used to do the actual read operations. We call this method
	 * instead of "forced_read" because it allows us to use all other methods in
	 * both the original CompactIndex and the derived class InMemoryCompactIndex.
	 **/
	virtual int readRawData(off_t where, void *buffer, int len);

	/**
	 * This is similar to readRawData. The method allocates a buffer big enough to
	 * hold the requested data and returns the pointer to the caller. The value of
	 * "mustBeFreed" indicates whether the memory has to be released by the caller,
	 * (calling "free") or not. This is used to avoid copying data from one memory
	 * address to another in the InMemoryCompactIndex.
	 **/
	virtual byte *getRawData(off_t where, int len, bool *mustBeFreed);

	/** Counterpart to readRawData. **/
	virtual int writeRawData(off_t where, void *buffer, int len);

	/**
	 * Stores the class name in the given buffer:
	 * "CompactIndex" or "InMemCompactIndex".
	 **/
	virtual void getClassName(char *target);

	/**
	 * Internal function, used by getPostings(char*). getPostings2 is a straight-
	 * forward implementation that performs a binary search on the term list and
	 * returns an ExtentList instance containing all postings for the given term.
	 * getPostings implements all the fancy stuff, like wildcards and stemming.
	 **/
	virtual ExtentList *getPostings2(const char *term);

	/**
	 * Returns an ExtentList_OR instance that contains the postings for all
	 * terms matching the given wildcard query. If "stem" is non-NULL,
	 * only the postings for terms who stem to "stem" are returned.
	 **/
	virtual ExtentList *getPostingsForWildcardQuery(const char *pattern, const char *stem);
	
}; // end of class CompactIndex


#endif


