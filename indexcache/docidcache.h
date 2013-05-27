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
 * The DocIdCache class is used to hold a compressed copy of all document IDs
 * in memory. This is crucial for high-performance TREC terabyte query processing.
 *
 * author: Stefan Buettcher
 * created: 2005-05-29
 * changed: 2007-08-06
 **/


#ifndef __INDEXCACHE__DOCIDCACHE_H
#define __INDEXCACHE__DOCIDCACHE_H


#include "../extentlist/extentlist.h"
#include "../index/index_types.h"
#include "../misc/lockable.h"


class DocIdCache : public Lockable {

public:

	static const int IDS_PER_BUCKET = 80;

	static const int INITIAL_BUCKET_SIZE = 32 * IDS_PER_BUCKET;

	static const int MAX_DOCID_LEN = 63;

protected:

	/** File handle of the data file. **/
	int fileHandle;

	/** Name of the data file. **/
	char *fileName;

private:

	/** Are we in read-only mode? **/
	bool readOnly;

	/**
	 * Tells us whether the data of this object have been modified. This is used
	 * to decide whether saveToDisk has to be called in the destructor.
	 **/
	bool modified;

	/** Number of documents for which we have an ID. **/
	int documentCount;

	/** Number of buckets for document IDs. **/
	int bucketCount;

	/** Number of buckets allocated. **/
	int bucketsAllocated;

	/** Size of the individual compressed docid buckets. **/
	int *bucketSize;

	/** Position of the first document in each bucket. **/
	offset *positions;

	/**
	 * An array of char array, each containing the compressed version of
	 * IDS_PER_BUCKET document IDs.
	 **/
	char **docIdBuckets;

	/**
	 * This is the currently active bucket. We keep it in uncompressed form for
	 * increased addDocumentID performance.
	 **/
	char *currentBucket;

	/** Number of bytes used inside the current bucket. **/
	int currentBucketSize;

	/** Number of bytes allocated for the current bucket. **/
	int currentBucketAllocated;

	/** Position of first and last document in current bucket. **/
	offset currentBucketFirstPos, currentBucketLastPos;

	/** Cached: Most recently used bucket. Speedup for sequential access patterns. **/
	int mruBucket, mruBucketSize;

	/** Data in most recently used bucket. **/
	byte *mruBucketData;

	/** Position of last access into MRU bucket. **/
	int positionInMruBucket;

	/** Offset of document whose ID was last returned. **/
	offset offsetOfMruDocument;

public:

	/** Default constructor. **/
	DocIdCache();

	/**
	 * Creates a DocIdCache instance with data file found at the given path. Depending
	 * on the value of "isDirectory", this path is either used directly as a file name,
	 * or the file name is constructed by appending "doc_ids" to the given directory
	 * path.
	 **/
	DocIdCache(const char *path, bool isDirectory);

	/** Releases all resources for the object. Saves modified data to the file. **/
	~DocIdCache();

	/** Writes the object's data to disk. **/
	void saveToDisk();

	/** Adds a new document ID to the DocIdCache. **/
	virtual void addDocumentID(offset documentStart, char *id);

	/**
	 * Returns a copy of the document ID for the document that was added as the
	 * documentNumber-th document to the cache. NULL if there is no such document.
	 * Memory has to be freed by the caller.
	 **/
	virtual char *getDocumentID(offset documentStart);

	/**
	 * Returns the n-th document ID stored inside this cache. If the ID does not
	 * exist (boundary check failed), NULL is returned. Memory has to be freed by
	 * by the caller.
	 **/
	virtual char *getNthDocumentID(offset n);

	/** Garbage collection for the document ID cache. **/
	virtual void filterAgainstFileList(ExtentList *files);

	/** Returns the number of buckets (including the one currently under construction). **/
	virtual int getBucketCount();

	/**
	 * Returns a '\n'-separated list of all document IDs in the given bucket. Memory
	 * has to be freed by the caller.
	 **/
	virtual char *getDocumentIDsInBucket(int whichBucket);

private:

	/** Loads the given bucket into memory and decompresses it for efficient access. **/
	void loadBucket(int whichBucket);

	/** Extracts the document ID for the given document start from the given buffer. **/
	char *extractID(byte *buffer, offset documentStart, int bufferSize);

	/**
	 * Same as above. However, in order to achieve a small speedup, we start the
	 * search for the document ID from address of the most recently accessed document
	 * ID in the same buffer, given by "positionInMruBucket" and "offsetOfMruDocument".
	 **/
	char *extractID(byte *buffer, offset documentStart, int bufferSize,
			int positionInMruBucket, offset offsetOfMruDocument);

	/** Does what it says. **/
	void releaseAllResources();

	/** Counterpart to saveToDisk(). **/
	void loadFromDisk();

}; // end of class DocIdCache


#endif


