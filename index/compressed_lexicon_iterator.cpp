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
 * The CompressedLexiconIterator works by obtaining a list for all terms in
 * the underlying CompressedLexicon instance. Whenever the iterator hits a
 * new term, it fetches *all* postings for that term from the CompressedLexicon
 * and stores them in a local buffer, in compressed form. Subsequent requests
 * for posting list segments for the current term are served from that buffer.
 *
 * author: Stefan Buettcher
 * created: 2005-04-24
 * changed: 2007-04-13
 **/


#include <string.h>
#include "compressed_lexicon_iterator.h"
#include "index.h"
#include "../misc/all.h"


CompressedLexiconIterator::CompressedLexiconIterator(CompressedLexicon *lexicon) {
	dataSource = lexicon;
	terms = dataSource->sortTerms();
	termCount = dataSource->termCount;
	termPos = 0;
	posInCurrentTermList = 0;
	if (termCount > 0)
		postingsForCurrentTerm = dataSource->terms[terms[termPos]].numberOfPostings;
	else
		postingsForCurrentTerm = 0;
	postingsFromCurrentTermFetched = 0;
	allCompressed = NULL;
	compressed = NULL;
	uncompressed = typed_malloc(offset, MAX_SEGMENT_SIZE);
	getNextChunk();
} // end of CompressedLexiconIterator(Lexicon*)


CompressedLexiconIterator::~CompressedLexiconIterator() {
	if (terms != NULL)
		free(terms);
	terms = NULL;
	if (allCompressed != NULL)
		free(allCompressed);
	allCompressed = NULL;
	if (compressed != NULL)
		free(compressed);
	compressed = NULL;
	if (uncompressed != NULL)
		free(uncompressed);
	uncompressed = NULL;
} // end of ~CompressedLexiconIterator()


void CompressedLexiconIterator::getNextChunk() {
	compressed = NULL;
	if (termPos >= termCount)
		return;

	CompressedLexiconEntry *term = &dataSource->terms[terms[termPos]];
	while (postingsFromCurrentTermFetched >= postingsForCurrentTerm) {
		termPos++;
		postingsFromCurrentTermFetched = 0;
		posInCurrentTermList = 0;
		if (termPos >= termCount)
			return;
		term = &dataSource->terms[terms[termPos]];
		postingsForCurrentTerm = term->numberOfPostings;
		if (dataSource->documentLevelIndexing >= 2)
			if (term->postingsInCurrentDocument < 32768) {
				postingsFromCurrentTermFetched = term->numberOfPostings;
				continue;
			}
		if (dataSource->owner->STEMMING_LEVEL >= 3)
			if ((term->stemmedForm >= 0) && (term->stemmedForm != terms[termPos])) {
				postingsFromCurrentTermFetched = term->numberOfPostings;
				continue;
			}
	} // end while (postingsFromCurrentTermFetched >= term->numberOfPostings)

	if (postingsFromCurrentTermFetched == 0) {
		// if we have too many postings to store them uncompressed, we have
		// to keep them in compressed form
		if (allCompressed != NULL) {
			free(allCompressed);
			allCompressed = NULL;
		}

		if (term->numberOfPostings == 1) {
			// we need special treatment for terms with only 1 posting here, since
			// these terms do not have a buffer containing compressed postings
			byte dummy[16];
			uncompressed[0] = term->lastPosting;
			postingsForCurrentTerm = 1;
			postingsFromCurrentTermFetched = 1;
			lengthOfCurrentChunk = 1;
			sizeOfCurrentChunk = 2 + encodeVByteOffset(term->lastPosting, dummy);
			return;
		} // end if (term->numberOfPostings == 1)
		else if (term->numberOfPostings < MIN_SEGMENT_SIZE) {
			// if the list is small enough to fit into a single chunk, don't do any
			// fancy stuff, but simply get the postings and return them as-is
			PostingList *list = dataSource->getPostingListForTerm(terms[termPos]);
			memcpy(uncompressed, list->postings, list->length * sizeof(offset));
			postingsForCurrentTerm = list->length;
			postingsFromCurrentTermFetched = list->length;
			lengthOfCurrentChunk = list->length;
			delete list;
	
			// determine compressed size of these postings
			byte dummy[16];
			sizeOfCurrentChunk = 1 + encodeVByte32(postingsForCurrentTerm, dummy);
			offset previous = 0;
			for (int i = 0; i < postingsForCurrentTerm; i++) {
				offset delta = uncompressed[i] - previous;
				sizeOfCurrentChunk += encodeVByteOffset(delta, dummy);
				previous += delta;
			}
			return;
		} // end if (term->numberOfPostings < MAX_SEGMENT_SIZE)
		else {
			int inputChunk = term->firstChunk;
			int chunkSize = 0;
			int totalSize = 16;

			// first pass: compute the memory requirements
			while (inputChunk >= 0) {
				byte *inputBuffer =
					&dataSource->containers[inputChunk >> CONTAINER_SHIFT][inputChunk & (CONTAINER_SIZE - 1)];
				chunkSize = inputBuffer[4];
				totalSize += chunkSize - 5;
				inputChunk = *((int32_t*)inputBuffer);
			}
			allCompressed = (byte*)malloc(totalSize);

			// second pass: fetch the compressed postings from the Lexicon
			int outPos = 16;

#if SUPPORT_APPEND_TAIT
			// if the current configuration has indexing-time support for append
			// operations, adjust all postings for the current term
			PostingList *list = dataSource->getPostingListForTerm(terms[termPos]);
			int count = list->length;
			offset *array = list->postings;
			outPos += encodeVByteOffset(array[0], &allCompressed[outPos]);
			for (int i = 1; i < count; i++)
				outPos += encodeVByteOffset(array[i] - array[i - 1], &allCompressed[outPos]);
			postingsForCurrentTerm = list->length;
			delete list;
#else
			inputChunk = term->firstChunk;
			while (inputChunk >= 0) {
				byte *inputBuffer =
					&dataSource->containers[inputChunk >> CONTAINER_SHIFT][inputChunk & (CONTAINER_SIZE - 1)];
				int nextChunk = *((int32_t*)inputBuffer);
				if (nextChunk >= 0)
					chunkSize = inputBuffer[4];
				else
					chunkSize = term->posInCurrentChunk;
				memcpy(&allCompressed[outPos], &inputBuffer[5], chunkSize - 5);
				outPos += (chunkSize - 5);
				inputChunk = nextChunk;
			} // end while (inputChunk >= 0)
			assert(outPos <= totalSize);
			postingsForCurrentTerm = term->numberOfPostings;
#endif
		} // end else [term->numberOfPostings > 1]

		lastPosting = 0;
		posInCurrentTermList = 16;
	} // end if (postingsFromCurrentTermFetched == 0)

	int yetToDo = postingsForCurrentTerm - postingsFromCurrentTermFetched;
	if (yetToDo <= MAX_SEGMENT_SIZE)
		lengthOfCurrentChunk = yetToDo;
	else if (yetToDo > TARGET_SEGMENT_SIZE + MAX_SEGMENT_SIZE)
		lengthOfCurrentChunk = TARGET_SEGMENT_SIZE;
	else
		lengthOfCurrentChunk = yetToDo / 2;

	// prepare two versions of the next chunk: a compressed one and an
	// uncompressed one; for the uncompressed one, we reserve new memory
	// for the compressed one, we simply point into the "allCompressed" buffer
	int inPos = posInCurrentTermList;
	int len = lengthOfCurrentChunk;
	byte dummy[16];
	int overhead = 1 + encodeVByte32(lengthOfCurrentChunk, dummy);

	for (int i = 0; i < len; i++) {
		int shift = 0;
		while (allCompressed[inPos] >= 128) {
			offset b = (allCompressed[inPos++] & 127);
			lastPosting += (b << shift);
			shift += 7;
		}
		offset b = allCompressed[inPos++];
		lastPosting += (b << shift);
		uncompressed[i] = lastPosting;
		if (i == 0)
			overhead += encodeVByteOffset(uncompressed[0], dummy) - (inPos - posInCurrentTermList);
	}

	sizeOfCurrentChunk = overhead + inPos - posInCurrentTermList;
	byte *current = compressed = &allCompressed[posInCurrentTermList - overhead];
	*(current++) = COMPRESSION_VBYTE;
	current += encodeVByte32(len, current);
	encodeVByteOffset(uncompressed[0], current);

	posInCurrentTermList = inPos;
	postingsFromCurrentTermFetched += len;
} // end of getNextChunk()


int64_t CompressedLexiconIterator::getTermCount() {
	return termCount;
}


int64_t CompressedLexiconIterator::getListCount() {
	return termCount;
}


bool CompressedLexiconIterator::hasNext() {
	if (termPos < termCount)
		return true;
	else
		return false;
} // end of hasNext()


char * CompressedLexiconIterator::getNextTerm() {
	if (termPos >= termCount)
		return NULL;
	return dataSource->terms[terms[termPos]].term;
} // end of getNextTerm()


PostingListSegmentHeader * CompressedLexiconIterator::getNextListHeader() {
	if (termPos >= termCount)
		return NULL;
	tempHeader.postingCount = lengthOfCurrentChunk;
	tempHeader.byteLength = sizeOfCurrentChunk;
	tempHeader.firstElement = uncompressed[0];
	tempHeader.lastElement = uncompressed[lengthOfCurrentChunk - 1];
	return &tempHeader;
} // end of getNextListHeader()


byte * CompressedLexiconIterator::getNextListCompressed(int *length, int *size, byte *buffer) {
	if (termPos >= termCount) {
		*length = *size = 0;
		return NULL;
	}

	*length = lengthOfCurrentChunk;
	if (compressed != NULL) {
		if (buffer == NULL)
			buffer = (byte*)malloc(sizeOfCurrentChunk);
		memcpy(buffer, compressed, sizeOfCurrentChunk);
		*size = sizeOfCurrentChunk;
	}
	else {
		if (buffer == NULL)
			buffer = compressVByte(uncompressed, lengthOfCurrentChunk, size);
		else {
			byte *temp = compressVByte(uncompressed, lengthOfCurrentChunk, size);
			memcpy(buffer, temp, *size);
			free(temp);
		}
		assert(*size == sizeOfCurrentChunk);
	}

	getNextChunk();
	return buffer;
} // end of getNextListCompressed(int*, int*, byte*)


offset * CompressedLexiconIterator::getNextListUncompressed(int *length, offset *buffer) {
	if (termPos >= termCount) {
		*length = 0;
		return NULL;
	}

	*length = lengthOfCurrentChunk;
	if (buffer == NULL)
		buffer = typed_malloc(offset, lengthOfCurrentChunk);
	memcpy(buffer, uncompressed, lengthOfCurrentChunk * sizeof(offset));

	getNextChunk();
	return buffer;
} // end of getNextListUncompressed(int*, offset*)


void CompressedLexiconIterator::skipNext() {
	if (termPos >= termCount)
		return;
	getNextChunk();
} // skipNext()


char * CompressedLexiconIterator::getClassName() {
	return duplicateString("CompressedLexiconIterator");
}


