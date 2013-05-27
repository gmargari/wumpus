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
 * The MultipleIndexIterator does essentially the same thing as the
 * vanilla CompactIndexIterator, but takes a bunch of CompactIndex instances
 * as input instead of only one.
 *
 * author: Stefan Buettcher
 * created: 2005-05-30
 * changed: 2007-04-03
 **/


#ifndef __INDEX__MULTIPLE_INDEX_ITERATOR_H
#define __INDEX__MULTIPLE_INDEX_ITERATOR_H


#include "index_iterator.h"
#include "index_types.h"


typedef struct {

	/** The iterator itself. **/
	IndexIterator *iterator;

	/** Tells us whether this iterator can deliver more data. **/
	int hasMoreData;

	/**
	 * What is the ID of the iterator? We need this for comparison. Iterators with
	 * lower ID come before iteraotrs with higher ID if terms are the same.
	 **/
	int iteratorID;

	/** Next/current term for this iterator. **/
	char nextTerm[MAX_TOKEN_LENGTH + 1];

	/** Next "previewSize" postings for the current term from the given input index. **/
	offset *preview;

	/** Number of postings in the preview, and number of postings already consumed. **/
	int previewSize, previewPos;

	offset nextPosting;

} IteratorDescriptor;


class MultipleIndexIterator : public IndexIterator {

private:

	/** Number of sub-iterators. **/
	int iteratorCount;

	/** References to the iterators and some management information. **/
	IteratorDescriptor *iterators;

	/** Heap structure to perform the n-way merge. **/
	int *iteratorHeap;

	/** Term currently being processed. Only used with SUPPORT_APPEND_TAIT. **/
	char currentTerm[MAX_TOKEN_LENGTH * 2];

	offset *postingsBuffer;
	int postingsInBuffer;
	offset *currentChunk;
	byte *currentChunkCompressed;
	int currentChunkLength, currentChunkSize;
	PostingListSegmentHeader currentHeader;

public:

	/**
	 * Class constructor. The resulting object will own the IndexIterator
	 * instances references by "iterators" as well as the array itself. It will
	 * be freed automatically inside the destructor.
	 **/
	MultipleIndexIterator(IndexIterator **iterators, int iteratorCount);

	virtual ~MultipleIndexIterator();

	virtual int64_t getTermCount();

	virtual int64_t getListCount();

	virtual bool hasNext();

	virtual char *getNextTerm();

	virtual PostingListSegmentHeader *getNextListHeader();

	virtual byte *getNextListCompressed(int *length, int *size, byte *buffer);

	virtual offset *getNextListUncompressed(int *length, offset *buffer);

	virtual void skipNext();

	virtual char *getClassName();

private:

	void determineNext(char *previousTerm);

	/** Restore heap property after extracting postings from the top list. **/
	void reheap();

	/** Helper method used when SUPPORT_APPEND_TAIT is turned on. **/
	void prepareNextChunk();

}; // end of class MultipleIndexIterator


#endif


