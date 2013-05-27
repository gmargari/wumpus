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
 * author: Stefan Buettcher
 * created: 2005-08-25
 * changed: 2006-05-10
 **/


#include <string.h>
#include "realloc_lexicon_iterator.h"
#include "index.h"
#include "../misc/all.h"


ReallocLexiconIterator::ReallocLexiconIterator(ReallocLexicon *lexicon) {
	dataSource = lexicon;
	terms = dataSource->sortTerms();
	termCount = dataSource->termCount;
	termPos = 0;
	posInCurrentTermList = 0;
	postingsFromCurrentTermFetched = 0;
	compressed = NULL;
	uncompressed = NULL;
	getNextChunk();
} // end of ReallocLexiconIterator(Lexicon*)


ReallocLexiconIterator::~ReallocLexiconIterator() {
	if (terms != NULL) {
		free(terms);
		terms = NULL;
	}
	if (uncompressed != NULL) {
		free(uncompressed);
		uncompressed = NULL;
	}
} // end of ~ReallocLexiconIterator()


void ReallocLexiconIterator::getNextChunk() {
	if (uncompressed != NULL) {
		free(uncompressed);
		uncompressed = NULL;
	}
	if (termPos >= termCount)
		return;

	ReallocLexiconEntry *term = &dataSource->terms[terms[termPos]];
	while (postingsFromCurrentTermFetched >= term->numberOfPostings) {
		termPos++;
		postingsFromCurrentTermFetched = 0;
		posInCurrentTermList = 0;
		if (termPos >= termCount)
			return;
		term = &dataSource->terms[terms[termPos]];
		if (dataSource->owner->STEMMING_LEVEL >= 3)
			if ((term->stemmedForm >= 0) && (term->stemmedForm != terms[termPos])) {
				postingsFromCurrentTermFetched = term->numberOfPostings;
				continue;
			}
	}

	// if we have too many postings to store them uncompressed, we have to return
	// postings in chunks, fed from the compressed array
	if (postingsFromCurrentTermFetched == 0) {
		compressed = term->postings;
		posInCurrentTermList = 0;
		lastPosting = 0;
		sizeOfCurrentTermList = term->numberOfPostings;
	}

	int yetToDo = term->numberOfPostings - postingsFromCurrentTermFetched;
	if (yetToDo <= MAX_SEGMENT_SIZE)
		lengthOfCurrentChunk = yetToDo;
	else if (yetToDo > TARGET_SEGMENT_SIZE + MAX_SEGMENT_SIZE)
		lengthOfCurrentChunk = TARGET_SEGMENT_SIZE;
	else
		lengthOfCurrentChunk = yetToDo / 2;

	int len = lengthOfCurrentChunk;
	int inPos = posInCurrentTermList;
	uncompressed = typed_malloc(offset, lengthOfCurrentChunk);
	for (int i = 0; i < len; i++) {
		int shift = 0;
		while (compressed[inPos] >= 128) {
			offset b = (compressed[inPos++] & 127);
			lastPosting += (b << shift);
			shift += 7;
		}
		offset b = compressed[inPos++];
		lastPosting += (b << shift);
		uncompressed[i] = lastPosting;
	}
	posInCurrentTermList = inPos;
	postingsFromCurrentTermFetched += lengthOfCurrentChunk;
	sizeOfCurrentChunk = 8 + inPos - posInCurrentTermList;
	posInCurrentTermList = inPos;

} // end of getListForNextTerm()


int64_t ReallocLexiconIterator::getTermCount() {
	return termCount;
}


int64_t ReallocLexiconIterator::getListCount() {
	return termCount;
}


bool ReallocLexiconIterator::hasNext() {
	if (termPos < termCount)
		return true;
	else
		return false;
} // end of hasNext()


char * ReallocLexiconIterator::getNextTerm() {
	if (termPos >= termCount)
		return NULL;
	return dataSource->terms[terms[termPos]].term;
} // end of getNextTerm()


PostingListSegmentHeader * ReallocLexiconIterator::getNextListHeader() {
	if (termPos >= termCount)
		return NULL;
	tempHeader.postingCount = lengthOfCurrentChunk;
	tempHeader.byteLength = sizeOfCurrentChunk;
	tempHeader.firstElement = uncompressed[0];
	tempHeader.lastElement = uncompressed[lengthOfCurrentChunk - 1];
	return &tempHeader;
} // end of getNextListHeader()


byte * ReallocLexiconIterator::getNextListCompressed(int *length, int *size) {
	if (termPos >= termCount) {
		*length = *size = 0;
		return NULL;
	}
	*length = lengthOfCurrentChunk;
	byte *result = compressVByte(uncompressed, lengthOfCurrentChunk, size);
	getNextChunk();
	return result;
} // end of getNextListCompressed(int*, int*)


offset * ReallocLexiconIterator::getNextListUncompressed(int *length) {
	if (termPos >= termCount) {
		*length = 0;
		return NULL;
	}
	*length = lengthOfCurrentChunk;
	offset *result = typed_malloc(offset, lengthOfCurrentChunk);
	memcpy(result, uncompressed, lengthOfCurrentChunk * sizeof(offset));
	getNextChunk();
	return result;
} // end of getNextListUncompressed(int*)


void ReallocLexiconIterator::skipNext() {
	if (termPos >= termCount)
		return;
	getNextChunk();
} // end of skipNext()

