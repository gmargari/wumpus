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
 * Implementation of the MultipleIndexIterator class.
 *
 * author: Stefan Buettcher
 * created: 2005-04-11
 * changed: 2007-04-03
 **/


#include <string.h>
#include "multiple_index_iterator.h"
#include "../misc/all.h"


static inline int compareIterators(IteratorDescriptor *a, IteratorDescriptor *b) {
	if ((a->hasMoreData) && (b->hasMoreData)) {
		int sCmp = strcmp(a->nextTerm, b->nextTerm);
		if (sCmp != 0)
			return sCmp;
		else
			return (a->iteratorID - b->iteratorID);
	}
	else
		return (b->hasMoreData - a->hasMoreData);
} // end of compareIterators(IteratorDescriptor*, IteratorDescriptor*)


MultipleIndexIterator::MultipleIndexIterator(IndexIterator **iterators, int iteratorCount) {
	assert(iterators != NULL);
	assert(iteratorCount > 0);

	// copy stuff from parameters and initialize local data structures
	this->iteratorCount = iteratorCount;
	this->iterators = typed_malloc(IteratorDescriptor, iteratorCount);
	listCount = 0;
	for (int i = 0; i < iteratorCount; i++) {
		listCount += iterators[i]->getListCount();
		this->iterators[i].iterator = iterators[i];
		this->iterators[i].iteratorID = i;
		char *term = iterators[i]->getNextTerm();
		if (term == NULL)
			this->iterators[i].hasMoreData = 0;
		else {
			this->iterators[i].hasMoreData = 1;
			strcpy(this->iterators[i].nextTerm, term);
		}
		this->iterators[i].preview = NULL;
	}

	// free the input array; we don't need it any more
	free(iterators);

	iteratorHeap = typed_malloc(int, iteratorCount);
#if SUPPORT_APPEND_TAIT
	for (int i = 0; i < iteratorCount; i++) {
		this->iterators[i].preview = typed_malloc(offset, MAX_SEGMENT_SIZE);
		this->iterators[i].previewPos = this->iterators[i].previewSize = 1;
	}
	postingsBuffer = typed_malloc(offset, MAX_SEGMENT_SIZE);
	postingsInBuffer = 0;
	currentChunk = typed_malloc(offset, MAX_SEGMENT_SIZE);
	currentChunkCompressed = NULL;
	strcpy(currentTerm, "");
	prepareNextChunk();
#else
	// create a heap of all input lists; use BubbleSort
	for (int i = 0; i < iteratorCount; i++)
		iteratorHeap[i] = i;
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < iteratorCount - 1; i++) {
			if (compareIterators(&this->iterators[iteratorHeap[i]], &this->iterators[iteratorHeap[i + 1]]) > 0) {
				int temp = iteratorHeap[i];
				iteratorHeap[i] = iteratorHeap[i + 1];
				iteratorHeap[i + 1] = temp;
				changed = true;
			}
		}
	}
	postingsBuffer = NULL;
	currentChunk = NULL;
	currentChunkCompressed = NULL;
#endif
} // end of MultipleIndexIterator(IndexIterator**, int)


MultipleIndexIterator::~MultipleIndexIterator() {
	for (int i = 0; i < iteratorCount; i++) {
		delete iterators[i].iterator;
		if (iterators[i].preview != NULL)
			free(iterators[i].preview);
	}
	free(iterators);
	iterators = NULL;
	free(iteratorHeap);
	iteratorHeap = NULL;

	if (postingsBuffer != NULL)
		free(postingsBuffer);
	postingsBuffer = NULL;
	if (currentChunk != NULL)
		free(currentChunk);
	currentChunk = NULL;
	if (currentChunkCompressed != NULL)
		free(currentChunkCompressed);
	currentChunkCompressed = NULL;
} // end of ~MultipleIndexIterator()


int64_t MultipleIndexIterator::getTermCount() {
	int result = 0;
	for (int i = 0; i < iteratorCount; i++)
		result += iterators[i].iterator->getTermCount();
	return result;
} // end of getTermCount()


int64_t MultipleIndexIterator::getListCount() {
	return listCount;
} // end of getListCount()


void MultipleIndexIterator::prepareNextChunk() {
	// this method is only used when SUPPORT_APPEND_TAIT is turned on

	while (true) {
		// obtain a certain number of postings from every input iterator
		bool allExhausted = true;
		for (int i = 0; i < iteratorCount; i++) {
			iteratorHeap[i] = i;
			IteratorDescriptor &iterator = iterators[i];
			if (iterator.previewPos < iterator.previewSize)
				allExhausted = false;
			else {
				iterator.previewPos = iterator.previewSize = 0;
				iterator.nextPosting = MAX_OFFSET;
				char *term = iterator.iterator->getNextTerm();
				if (term != NULL)
					if (strcmp(term, currentTerm) == 0) {
						iterator.iterator->getNextListUncompressed(&iterator.previewSize, iterator.preview);
						assert(iterator.previewSize > 0);
						iterator.nextPosting = iterator.preview[0];
						allExhausted = false;
					}
			}
		} // end for (int i = 0; i < iteratorCount; i++)

		if (!allExhausted)
			break;

		// if we are unable to find a single input iterator that still has postings
		// left for the given term, advance to next term (or terminate if no term left)
		currentTerm[0] = 0;
		for (int i = 0; i < iteratorCount; i++) {
			char *term = iterators[i].iterator->getNextTerm();
			if (term != NULL)
				if ((currentTerm[0] == 0) || (strcmp(currentTerm, term) > 0))
					strcpy(currentTerm, term);
		}
		if (currentTerm[0] == 0)
			return;
	} // end while (true)

	// establish heap property by means of BubbleSort
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < iteratorCount - 1; i++) {
			if (iterators[iteratorHeap[i]].nextPosting > iterators[iteratorHeap[i + 1]].nextPosting) {
				int temp = iteratorHeap[i];
				iteratorHeap[i] = iteratorHeap[i + 1];
				iteratorHeap[i + 1] = temp;
				changed = true;
			}
		}
	} // end while (changed)

	while (postingsInBuffer < MAX_SEGMENT_SIZE) {
		int top = iteratorHeap[0];
		IteratorDescriptor &iterator = iterators[top];
		if (iterator.nextPosting >= MAX_OFFSET)
			break;

		postingsBuffer[postingsInBuffer++] = iterator.nextPosting;
		if (++iterator.previewPos >= iterator.previewSize) {
			iterator.previewPos = iterator.previewSize = 0;
			iterator.nextPosting = MAX_OFFSET;
			char *term = iterator.iterator->getNextTerm();
			if (term != NULL)
				if (strcmp(term, currentTerm) == 0) {
					iterator.iterator->getNextListUncompressed(&iterator.previewSize, iterator.preview);
					assert(iterator.previewSize > 0);
					iterator.nextPosting = iterator.preview[0];
				}
		}
		else
			iterator.nextPosting = iterator.preview[iterator.previewPos];

		// restore heap property
		int parent = 0, child = 1;
		while (child < iteratorCount) {
			if (child < iteratorCount - 1)
				if (iterators[iteratorHeap[child]].nextPosting > iterators[iteratorHeap[child + 1]].nextPosting)
					child++;
			if (iterator.nextPosting <= iterators[iteratorHeap[child]].nextPosting)
				break;
			iteratorHeap[parent] = iteratorHeap[child];
			iteratorHeap[child] = top;
			parent = child;
			child = parent + parent + 1;
		}
	} // end while (postingsInBuffer < MAX_SEGMENT_SIZE)

	assert(postingsInBuffer > 0);

	// take first MAX_SEGMENT_SIZE/2 or so postings and transform them into a
	// proper list segment
	if (postingsInBuffer < MAX_SEGMENT_SIZE)
		currentChunkLength = postingsInBuffer;
	else
		currentChunkLength = postingsInBuffer / 2;

	memcpy(currentChunk, postingsBuffer, currentChunkLength * sizeof(offset));
	postingsInBuffer -= currentChunkLength;
	memmove(postingsBuffer, &postingsBuffer[currentChunkLength], postingsInBuffer * sizeof(offset));
	if (currentChunkCompressed != NULL)
		free(currentChunkCompressed);
	currentChunkCompressed =
		compressVByte(currentChunk, currentChunkLength, &currentChunkSize);
	currentHeader.postingCount = currentChunkLength;
	currentHeader.byteLength = currentChunkSize;
	currentHeader.firstElement = currentChunk[0];
	currentHeader.lastElement = currentChunk[currentChunkLength - 1];
} // end of prepareNextChunk()


bool MultipleIndexIterator::hasNext() {
#if SUPPORT_APPEND_TAIT
	return (currentTerm[0] != 0);
#else
	return (iterators[iteratorHeap[0]].hasMoreData);
#endif
} // end of hasNext()


char * MultipleIndexIterator::getNextTerm() {
#if SUPPORT_APPEND_TAIT
	return (currentTerm[0] == 0 ? NULL : currentTerm);
#else
	if (iterators[iteratorHeap[0]].hasMoreData)
		return iterators[iteratorHeap[0]].nextTerm;
	else
		return NULL;
#endif
} // end of getNextTerm()


PostingListSegmentHeader * MultipleIndexIterator::getNextListHeader() {
#if SUPPORT_APPEND_TAIT
	return (currentTerm[0] == 0 ? NULL : &currentHeader);
#else
	if (iterators[iteratorHeap[0]].hasMoreData)
		return iterators[iteratorHeap[0]].iterator->getNextListHeader();
	else
		return NULL;
#endif
} // end of getNextListHeader()


byte * MultipleIndexIterator::getNextListCompressed(int *length, int *size, byte *buffer) {
#if SUPPORT_APPEND_TAIT
	*length = currentChunkLength;
	*size = currentChunkSize;
	byte *result;
	if (buffer == NULL)
		buffer = currentChunkCompressed;
	else {
		memcpy(buffer, currentChunkCompressed, currentChunkSize);
		free(currentChunkCompressed);
	}
	currentChunkCompressed = NULL;
	prepareNextChunk();
	return buffer;
#else
	int top = iteratorHeap[0];
	if (!iterators[top].hasMoreData) {
		*length = 0;
		*size = 0;
		return NULL;
	}
	buffer = iterators[top].iterator->getNextListCompressed(length, size, buffer);
	listPos++;
	reheap();
	return buffer;
#endif
} // end of getNextListCompressed(int*, int*, byte*)


offset * MultipleIndexIterator::getNextListUncompressed(int *length, offset *buffer) {
#if SUPPORT_APPEND_TAIT
	*length = currentChunkLength;
	if (buffer == NULL)
		buffer = typed_malloc(offset, currentChunkLength);
	memcpy(buffer, currentChunk, currentChunkLength * sizeof(offset));
	free(currentChunkCompressed);
	currentChunkCompressed = NULL;
	prepareNextChunk();
	return buffer;
#else
	int top = iteratorHeap[0];
	if (!iterators[top].hasMoreData) {
		*length = 0;
		return NULL;
	}
	offset *result = iterators[top].iterator->getNextListUncompressed(length, buffer);
	listPos++;
	reheap();
	return result;
#endif
} // end of getNextListUncompressed(int*, offset*)


void MultipleIndexIterator::skipNext() {
#if SUPPORT_APPEND_TAIT
	if (currentChunkCompressed != NULL)
		free(currentChunkCompressed);
	currentChunkCompressed = NULL;
	prepareNextChunk();
#else
	int length, size;
	byte *dummy = getNextListCompressed(&length, &size, NULL);
	free(dummy);
#endif
} // end of skipNext()


void MultipleIndexIterator::reheap() {
	// update meta-data for top of heap
	int top = iteratorHeap[0];
	char *nextTerm = iterators[top].iterator->getNextTerm();
	if (nextTerm == NULL)
		iterators[top].hasMoreData = 0;
	else {
		iterators[top].hasMoreData = 1;
		strcpy(iterators[top].nextTerm, nextTerm);
	}

	// perform re-heap operation
	int parent = 0;
	int child = 1;
	while (child < iteratorCount) {
		if (child + 1 < iteratorCount)
			if (compareIterators(&iterators[iteratorHeap[child + 1]], &iterators[iteratorHeap[child]]) < 0)
				child++;
		if (compareIterators(&iterators[iteratorHeap[parent]], &iterators[iteratorHeap[child]]) <= 0)
			break;
		int temp = iteratorHeap[parent];
		iteratorHeap[parent] = iteratorHeap[child];
		iteratorHeap[child] = temp;
		parent = child;
		child = parent + parent + 1;
	}
} // end of reheap()


char * MultipleIndexIterator::getClassName() {
	return duplicateString("MultipleIndexIterator");
}


