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
 * The IndexIterator class is used to iterate over the content of any Index
 * instance (CompactIndex, Lexicon, ...). It reads data from an existent index
 * instance and returns one chunk of postings (list segment) at a time.
 *
 * author: Stefan Buettcher
 * created: 2005-04-11
 * changed: 2007-07-16
 **/


#ifndef __INDEX__INDEX_ITERATOR_H
#define __INDEX__INDEX_ITERATOR_H


#include "compactindex.h"


class IndexIterator : public Lockable {

	friend class CompactIndex;
	friend class HybridLexicon;
	friend class MultipleIndexIterator;

public:

	static const int MIN_BUFFER_SIZE = 32768;

	static const int MAX_BUFFER_SIZE = 4 * 1024 * 1024;

protected:

	/** Name of the input file. **/
	char *fileName;

	/** Handle to the input file. **/
	int fileHandle;

	/** Buffer used to speed up index processing. **/
	byte *readBuffer;

	/** Current position inside the read buffer. **/
	int bufferPos;

	/** Number of bytes inside the read buffer. **/
	int bufferSize;

	/** Maximum number of bytes to read into the buffer. **/
	int maxBufferSize;

	/** Number of distinct terms in the index. **/
	int64_t termCount;

	/** Number of lists inside the CompactIndex instance. **/
	int64_t listCount;

	/** Position inside the list of lists (number of lists done). **/
	int64_t listPos;

	/** Current term inside the CompactIndex (next-to-be-returned). **/
	char currentTerm[MAX_TOKEN_LENGTH + 1];

	PostingListSegmentHeader currentHeaders[CompactIndex::MAX_SEGMENTS_IN_MEMORY];
	int32_t currentSegmentCount;
	int32_t currentSegmentPos;

protected:

	/** Default constructor. **/
	IndexIterator();

	/**
	 * Creates a new IndexIterator instance that reads data from "fileName"
	 * and uses a read buffer of size "bufferSize".
	 **/
	IndexIterator(const char *fileName, int bufferSize);

public:

	/** Destructor. **/
	virtual ~IndexIterator();

	/** Returns the number of terms in the index. **/
	virtual int64_t getTermCount();

	/** Returns the total number of list segments inside the index. **/
	virtual int64_t getListCount();

	/** Returns true iff there are more data to be returned. **/
	virtual bool hasNext();

	/**
	 * Returns a pointer to the next term inside the CompactIndex. No not touch
	 * the pointer or the data referenced, man! Do not free it! Returns NULL if
	 * there is no more term (end of index reached).
	 **/
	virtual char *getNextTerm();

	/**
	 * Returns the header of the next list or NULL if the end of the index has been
	 * reached. Do not touch it, do not free it.
	 **/
	virtual PostingListSegmentHeader *getNextListHeader();

	/**
	 * Returns the compressed posting list for the current position in the index.
	 * The length of the list (number of postings) is written to the memory
	 * referenced by "length", the size (number of bytes) to "size". If "buffer"
	 * is non-NULL, then the given buffer is used to store the postings.
	 **/
	virtual byte *getNextListCompressed(int *length, int *size, byte *buffer);

	/**
	 * Returns the uncompressed posting list for the current position in the index.
	 * The length of the list (number of postings) is written to the memory
	 * referenced by "length". If a buffer is given (non-NULL), then that buffer
	 * is used to store the postings.
	 **/
	virtual offset *getNextListUncompressed(int *length, offset *buffer);

	virtual void skipNext();

	virtual char *getClassName();

protected:

	/** Makes sure the cache contains at least this many bytes. **/
	virtual void ensureCacheIsFull(int bytesNeeded);

	/** Loads term and segment descriptors for next bunch of segments. **/
	virtual void loadNextTerm();

}; // end of class IndexIterator


#endif

