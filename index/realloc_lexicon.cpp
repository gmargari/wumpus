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
 * Implementation of the ReallocLexicon class.
 *
 * author: Stefan Buettcher
 * created: 2005-08-25
 * changed: 2007-04-04
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "realloc_lexicon.h"
#include "index.h"
#include "index_iterator.h"
#include "index_merger.h"
#include "realloc_lexicon_iterator.h"
#include "segmentedpostinglist.h"
#include "../filesystem/filefile.h"
#include "../misc/all.h"
#include "../query/query.h"
#include "../stemming/stemmer.h"


static const char *LOG_ID = "ReallocLexicon";

const double ReallocLexicon::SLOT_GROWTH_RATE;


ReallocLexicon::ReallocLexicon(Index *owner, int documentLevelIndexing) {
	this->owner = owner;

	// initialize data
	termCount = 0;
	termSlotsAllocated = INITIAL_SLOT_COUNT;
	terms = typed_malloc(ReallocLexiconEntry, termSlotsAllocated);
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = -1;

	// update "occupied memory" information
	memoryOccupied = termSlotsAllocated * sizeof(ReallocLexiconEntry);
	memoryOccupied += HASHTABLE_SIZE * sizeof(int32_t);
} // end of ReallocLexicon(Index*, int)


ReallocLexicon::~ReallocLexicon() {
	if (terms != NULL) {
		for (int i = 0; i < termCount; i++)
			if (terms[i].postings != NULL)
				free(terms[i].postings);
		free(terms);
		terms = NULL;
	}
} // end of ReallocLexicon()


void ReallocLexicon::clear() {
	bool mustReleaseWriteLock = getWriteLock();

	// release all resources
	for (int i = 0; i < termCount; i++)
		if (terms[i].postings != NULL)
			free(terms[i].postings);
	free(terms);
	termCount = 0;
	termSlotsAllocated = INITIAL_SLOT_COUNT;
	terms = typed_malloc(ReallocLexiconEntry, termSlotsAllocated);
	
	// virginize hashtable
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = -1;

	// update occupied memory information
	memoryOccupied = termSlotsAllocated * sizeof(ReallocLexiconEntry);
	memoryOccupied += HASHTABLE_SIZE * sizeof(int32_t);

	// update coverage information
	firstPosting = MAX_OFFSET;
	lastPosting = 0;

	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of clear()


void ReallocLexicon::clear(int threshold) {
	assert("Not implemented yet!" == NULL);
} // end of clear(int)


void ReallocLexicon::extendTermsArray() {
	memoryOccupied -= termSlotsAllocated * sizeof(ReallocLexiconEntry);
	termSlotsAllocated = (int)(termCount * SLOT_GROWTH_RATE);
	if (termSlotsAllocated < termCount + INITIAL_SLOT_COUNT)
		termSlotsAllocated = termCount + INITIAL_SLOT_COUNT;
	typed_realloc(ReallocLexiconEntry, terms, termSlotsAllocated);
	memoryOccupied += termSlotsAllocated * sizeof(ReallocLexiconEntry);
} // end of extendTermsArray()


int32_t ReallocLexicon::addPosting(char *term, offset posting, unsigned int hashValue) {
	// search the hashtable for the given term
	unsigned int hashSlot = hashValue % HASHTABLE_SIZE;
	int termID = hashtable[hashSlot];
	int previous = termID;
	int stemmingLevel = owner->STEMMING_LEVEL;

	while (termID >= 0) {
		if (terms[termID].hashValue == hashValue) {
			if (strcmp(term, terms[termID].term) == 0)
				break;
		}
		previous = termID;
		termID = terms[termID].nextTerm;
	}

	// if the term cannot be found in the lexicon, add a new entry
	if (termID < 0) {
		// termID < 0 means the term does not exist so far: create a new entry
		if (termCount >= termSlotsAllocated)
			extendTermsArray();

		// add new term slot as head of hash list
		termID = termCount++;
		strcpy(terms[termID].term, term);
		terms[termID].hashValue = hashValue;
		terms[termID].nextTerm = hashtable[hashSlot];
		hashtable[hashSlot] = termID;

		terms[termID].numberOfPostings = 1;
		terms[termID].lastPosting = posting;
		terms[termID].postings = NULL;

		// set "stemmedForm" according to the situation; apply stemming if
		// STEMMING_LEVEL > 0
		int len = strlen(term);
		if (term[len - 1] == '$')
			terms[termID].stemmedForm = -1;
		else if (stemmingLevel > 0) {
			char stem[MAX_TOKEN_LENGTH * 2];
			Stemmer::stemWord(term, stem, LANGUAGE_ENGLISH, false);
			if (stem[0] == 0)
				terms[termID].stemmedForm = termID;
			else if ((stemmingLevel < 2) && (strcmp(stem, term) == 0))
				terms[termID].stemmedForm = termID;
			else {
				len = strlen(stem);
				if (len >= MAX_TOKEN_LENGTH - 1) {
					stem[MAX_TOKEN_LENGTH - 1] = '$';
					stem[MAX_TOKEN_LENGTH] = 0;
				}
				else {
					stem[len] = '$';
					stem[len + 1] = 0;
				}
				int32_t stemmed = addPosting(stem, posting, getHashValue(stem));
				terms[termID].stemmedForm = stemmed;
			}
		}
		else
			terms[termID].stemmedForm = termID;
	} // end if (termID < 0)

	else {
		// move term to front of list in hashtable
		if (previous != termID) {
			terms[previous].nextTerm = terms[termID].nextTerm;
			terms[termID].nextTerm = hashtable[hashSlot];
			hashtable[hashSlot] = termID;
		}

		// we only add more than the first posting if:
		// - we are in STEMMING_LEVEL < 3 (means: we keep non-stemmed terms) or
		// - the term is not stemmable (stemmedForm == termID) or
		// - the term is already the stemmed form (stemmedForm < 0)
		if ((stemmingLevel >= 3) && (terms[termID].stemmedForm >= 0) && (terms[termID].stemmedForm != termID))
			goto beforeAddingPostingForStemmedForm;

		if (posting <= terms[termID].lastPosting) {
			snprintf(errorMessage, sizeof(errorMessage),
					"Postings not monotonically increasing: %lld, %lld",
					(long long)terms[termID].lastPosting, (long long)posting);
			log(LOG_ERROR, LOG_ID, errorMessage);
			goto addPosting_endOfBitgeficke;
		}

		if (terms[termID].numberOfPostings <= 1) {
			if (terms[termID].numberOfPostings == 0) {
				// we have no postings yet for this guy; this can only happen if it is
				// one of the survivor terms from an earlier part of the text collection;
				// data has already been initialized, so we don't need to do anything here
				terms[termID].lastPosting = posting;
			}
			else {
				// in this case, no chunk has been created yet; so, create the first
				// chunk and move both the first and the new posting into that chunk
				memoryOccupied += INITIAL_CHUNK_SIZE + sizeof(void*);
				byte *chunkData = terms[termID].postings = (byte*)malloc(INITIAL_CHUNK_SIZE);
				terms[termID].bufferSize = INITIAL_CHUNK_SIZE;
				int posInChunk = 0;
				offset value = terms[termID].lastPosting;
				while (value >= 128) {
					chunkData[posInChunk++] = 128 + (value & 127);
					value >>= 7;
				}
				chunkData[posInChunk++] = value;
				value = posting - terms[termID].lastPosting;
				while (value >= 128) {
					chunkData[posInChunk++] = 128 + (value & 127);
					value >>= 7;
				}
				chunkData[posInChunk++] = value;
				terms[termID].bufferPos = posInChunk;
			}
		} // end if (terms[termID].numberOfPostings <= 1)
		else {
			// we already have stuff in the chunks, so just append...
			int posInChunk = terms[termID].bufferPos;
			int sizeOfChunk = terms[termID].bufferSize;
			byte *chunkData = terms[termID].postings;
			// let "value" contain the Delta value with respect to the previous posting
			offset value = posting - terms[termID].lastPosting;
			if (posInChunk < sizeOfChunk - 6) {
				// if we have enough free space (42 bits are enough here because we probably
				// cannot have more than 2^42 postings in memory at the same time)
				while (value >= 128) {
					chunkData[posInChunk++] = 128 + (value & 127);
					value >>= 7;
				}
				chunkData[posInChunk++] = value;
			}
			else {
				// if less than 42 bits are free, we might have to allocate a new chunk...
				while (true) {
					if (posInChunk >= sizeOfChunk) {
						int newSize = sizeOfChunk + ((sizeOfChunk * CHUNK_GROWTH_RATE) >> 5);
						if (newSize < sizeOfChunk + INITIAL_CHUNK_SIZE)
							newSize = sizeOfChunk + INITIAL_CHUNK_SIZE;
						memoryOccupied += (newSize - sizeOfChunk);
						terms[termID].bufferSize = sizeOfChunk = newSize;
						terms[termID].postings = chunkData = (byte*)realloc(chunkData, newSize);
					}
					if (value < 128) {
						chunkData[posInChunk++] = value;
						break;
					}
					else {
						chunkData[posInChunk++] = 128 + (value & 127);
						value >>= 7;
					}
				}
			} // end else [less than 42 bits free in current chunk]
			terms[termID].bufferPos = posInChunk;
		} // end else [numberOfPostings >= 2]
		terms[termID].lastPosting = posting;
		terms[termID].numberOfPostings++;

beforeAddingPostingForStemmedForm:

		// add posting for stemmed form, if desired
		int stemmedForm = terms[termID].stemmedForm;
		if ((stemmedForm >= 0) && (stemmedForm != termID))
			addPosting(terms[stemmedForm].term, posting, terms[stemmedForm].hashValue);
	} // end else [termID >= 0]

addPosting_endOfBitgeficke:

	return termID;
} // end of addPosting(char*, offset, unsigned int)


void ReallocLexicon::addPostings(char **terms, offset *postings, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	for (int i = 0; i < count; i++)
		ReallocLexicon::addPosting(terms[i], postings[i], getHashValue(terms[i]));
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(char**, offset*, int)


void ReallocLexicon::addPostings(char *term, offset *postings, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	unsigned int hashValue = getHashValue(term);
	for (int i = 0; i < count; i++)
		ReallocLexicon::addPosting(term, postings[i], hashValue);
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(char*, offset*, int)


void ReallocLexicon::addPostings(InputToken *terms, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	for (int i = 0; i < count; i++)
		ReallocLexicon::addPosting((char*)terms[i].token, terms[i].posting, terms[i].hashValue);
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(InputToken*, int)


void ReallocLexicon::createCompactIndex(const char *fileName) {
	assert(termCount > 0);

	bool mustReleaseReadLock = getReadLock();

	int stemmingLevel = owner->STEMMING_LEVEL;
	int32_t *sortedTerms = sortTerms();
	offset outputBuffer[TARGET_SEGMENT_SIZE * 2];
	CompactIndex *target = CompactIndex::getIndex(owner, fileName, true);

	for (int i = 0; i < termCount; i++) {
		int termID = sortedTerms[i];
		char *currentTerm = terms[termID].term;
		int outputBufferPos = 0;

		// if requested, discard all unstemmed-but-stemmable term information
		if (stemmingLevel >= 3)
			if ((terms[termID].stemmedForm >= 0) && (terms[termID].stemmedForm != termID))
				continue;

		// special handling for terms without postings or with only 1 posting
		if (terms[termID].numberOfPostings <= 1) {
			if (terms[termID].numberOfPostings > 0)
				outputBuffer[outputBufferPos++] = terms[termID].lastPosting;
		}
		else {
			offset dummy, lastOffset = 0;
			byte *inputBuffer = terms[termID].postings;
			int chunkPos = 0, postingCount = terms[termID].numberOfPostings, shift = 0;

			for (int i = 0; i < postingCount; i++) {
				byte b = inputBuffer[chunkPos++];
				while (b >= 128) {
					dummy = (b & 127);
					lastOffset += (dummy << shift);
					shift += 7;
					b = inputBuffer[chunkPos++];
				}
				dummy = b;
				lastOffset += (dummy << shift);
				outputBuffer[outputBufferPos++] = lastOffset;
				shift = 0;
				if (outputBufferPos >= TARGET_SEGMENT_SIZE)
					if (i < postingCount - 16) {
						target->addPostings(currentTerm, outputBuffer, outputBufferPos);
						outputBufferPos = 0;
					}
			} // end for (int i = 0; i < postingCount; i++)

		} // end else [terms[termID].numberOfPostings > 1]

createCompactIndex_decodingFinished:

		if (outputBufferPos > 0) {
			assert(outputBuffer[outputBufferPos - 1] == terms[termID].lastPosting);
			target->addPostings(currentTerm, outputBuffer, outputBufferPos);
			outputBufferPos = 0;
		}
	} // end for (int i = 0; i < termCount; i++)

	free(sortedTerms);

	delete target;
	if (mustReleaseReadLock)
		releaseReadLock();
} // end of createCompactIndex(char*)


void ReallocLexicon::mergeWithExisting(
			IndexIterator **iterators, int iteratorCount, char *outputIndex) {
	if (iterators == NULL) {
		createCompactIndex(outputIndex);
		return;
	}

	bool mustReleaseReadLock = getReadLock();

	IndexIterator **newIterators = typed_malloc(IndexIterator*, iteratorCount + 1);
	for (int i = 0; i < iteratorCount; i++)
		newIterators[i] = iterators[i];
	newIterators[iteratorCount] = new ReallocLexiconIterator(this);
	free(iterators);
	iterators = newIterators;
	iteratorCount++;

	IndexMerger::mergeIndices(owner, outputIndex, iterators, iteratorCount);

	if (mustReleaseReadLock)
		releaseReadLock();
} // end of mergeWithExisting(...)


void ReallocLexicon::mergeWithExisting(
			IndexIterator **iterators, int iteratorCount, char *outputIndex, ExtentList *visible) {
	bool mustReleaseReadLock = getReadLock();

	IndexIterator **newIterators = typed_malloc(IndexIterator*, iteratorCount + 1);
	for (int i = 0; i < iteratorCount; i++)
		newIterators[i] = iterators[i];
	newIterators[iteratorCount] = new ReallocLexiconIterator(this);
	if (iterators != NULL)
		free(iterators);
	iterators = newIterators;
	iteratorCount++;

	IndexMerger::mergeIndicesWithGarbageCollection(owner, outputIndex,
			iterators, iteratorCount, visible);

	if (mustReleaseReadLock)
		releaseReadLock();
} // end of mergeWithExisting(...)


static int termComparator(const void *a, const void *b) {
	ReallocLexiconEntry *x = (ReallocLexiconEntry*)a;
	ReallocLexiconEntry *y = (ReallocLexiconEntry*)b;
	return strcmp(x->term, y->term);
}


void ReallocLexicon::selectionSort(ReallocLexiconEntry *terms,
			int32_t *idArray, int count) {
	for (int i = 0; i < count; i++) {
		int best = i;
		for (int j = i + 1; j < count; j++)
			if (strcmp(terms[idArray[j]].term, terms[idArray[best]].term) < 0)
				best = j;
		int32_t temp = idArray[i];
		idArray[i] = idArray[best];
		idArray[best] = temp;
	}
} // end of selectionSort(ReallocLexiconEntry*, int32_t*, int)


void ReallocLexicon::mergeSort(ReallocLexiconEntry *terms, int32_t *idArray,
			int32_t *tempArray, int count) {
	if (count < 12)
		selectionSort(terms, idArray, count);
	else {
		int middle = (count >> 1);
		mergeSort(terms, idArray, tempArray, middle);
		mergeSort(terms, &idArray[middle], tempArray, count - middle);
		int leftPos = 0;
		int rightPos = middle;
		int outPos = 0;
		while (true) {
			int comparison = strcmp(terms[idArray[leftPos]].term, terms[idArray[rightPos]].term);
			if (comparison <= 0) {
				tempArray[outPos++] = idArray[leftPos++];
				if (leftPos >= middle)
					break;
			}
			else {
				tempArray[outPos++] = idArray[rightPos++];
				if (rightPos >= count)
					break;
			}
		}
		while (leftPos < middle)
			tempArray[outPos++] = idArray[leftPos++];
		while (rightPos < count)
			tempArray[outPos++] = idArray[rightPos++];
		memcpy(idArray, tempArray, count * sizeof(int32_t));
	}
} // end of mergeSort(ReallocLexiconEntry*, int32_t*, int32_t*, int)


void ReallocLexicon::hybridBucketSort(ReallocLexiconEntry *terms, int32_t *idArray, int count) {
	if (count < 65536) {
		int32_t *temp = typed_malloc(int32_t, count);
		mergeSort(terms, idArray, temp, count);
		free(temp);
		return;
	}

	int32_t *subListLength = typed_malloc(int32_t, 65536);
	int32_t **subLists = typed_malloc(int32_t*, 65536);
	for (int i = 0; i < 65536; i++)
		subListLength[i] = 0;

	// first pass: compute the lengths of the sublists
	for (int i = 0; i < count; i++) {
		byte *term = (byte*)terms[idArray[i]].term;
		int byte1 = term[0];
		int byte2 = term[1];
		subListLength[(byte1 << 8) + byte2]++;
	}

	// allocate memory for sublists
	int maxSubListLength = 0;
	for (int i = 0; i < 65536; i++) {
		int length = subListLength[i];
		if (length > 0) {
			subLists[i] = typed_malloc(int32_t, length);
			if (length > maxSubListLength)
				maxSubListLength = length;
		}
		else
			subLists[i] = NULL;
		subListLength[i] = 0;
	}

	// second pass: put terms in the appropriate sublists
	for (int i = 0; i < count; i++) {
		byte *term = (byte*)terms[idArray[i]].term;
		int byte1 = term[0];
		int byte2 = term[1];
		int bucket = (byte1 << 8) + byte2;
		subLists[bucket][subListLength[bucket]++] = idArray[i];
	}

	// run mergeSort on the sublists and put them back into the original list
	int32_t *temp = typed_malloc(int32_t, maxSubListLength);
	int outCnt = 0;
	for (int i = 0; i < 65536; i++)
		if (subLists[i] != NULL) {
			mergeSort(terms, subLists[i], temp, subListLength[i]);
			memcpy(&idArray[outCnt], subLists[i], subListLength[i] * sizeof(int32_t));
			outCnt += subListLength[i];
			free(subLists[i]);
			subLists[i] = NULL;
		}
	assert(outCnt == count);

	// free memory
	free(temp);
	free(subLists);
	free(subListLength);
} // end of hybridBucketSort()


int32_t * ReallocLexicon::sortTerms() {
	int32_t *result = typed_malloc(int32_t, termCount);
	for (int i = 0; i < termCount; i++)
		result[i] = i;
	hybridBucketSort(terms, result, termCount);
	return result;
} // end of sortTerms()


ExtentList * ReallocLexicon::getUpdates(const char *term) {
	bool mustReleaseReadLock = getReadLock();
	ExtentList *result = NULL;

	int termLen = strlen(term);
	if (term[termLen - 1] == '*') {
		for (int i = 0; i < termLen - 1; i++)
			if ((term[i] == '$') || (term[i] == '*'))
				termLen = -1;
		if (termLen < 3) {
			result = new ExtentList_Empty();
		}
		else {
			int matchedCnt = 0;
			int matchedAllocated = 32;
			ExtentList **matches = typed_malloc(ExtentList*, matchedAllocated);
			for (int i = 0; i < termCount; i++)
				if (strncmp(term, terms[i].term, termLen - 1) == 0) {
					if (matchedCnt >= matchedAllocated) {
						matchedAllocated *= 2;
						typed_realloc(ExtentList*, matches, matchedAllocated);
					}
					matches[matchedCnt++] = getPostingListForTerm(i);
				}
			if (matchedCnt <= 1) {
				if (matchedCnt == 0)
					result = new ExtentList_Empty();
				else
					result = matches[0];
				free(matches);
			}
			else
				result = new ExtentList_OR(matches, matchedCnt);
		}
	} // end if (term[termLen - 1] == '*')
	else if ((term[termLen - 1] == '$') && (owner->STEMMING_LEVEL < 2)) {
		char withoutDollarSymbol[MAX_TOKEN_LENGTH * 2];
		strcpy(withoutDollarSymbol, term);
		withoutDollarSymbol[termLen - 1] = 0;
		int matchedCnt = 0;
		int matchedAllocated = 32;
		ExtentList **matches = typed_malloc(ExtentList*, matchedAllocated);
		
		int len = (termLen > 4 ? termLen - 2 : termLen - 1);

		// don't care whether we are in stemming level 0 or 1 here, simply scan over
		// all terms in the lexicon and check whether they match the query term; it's
		// all in memory anyway, so it won't make much of a difference
		for (int i = 0; i < termCount; i++) {
			if (strncmp(withoutDollarSymbol, terms[i].term, len) == 0) {
				char stemmed[MAX_TOKEN_LENGTH * 2];
				Stemmer::stemWord(terms[i].term, stemmed, LANGUAGE_ENGLISH, false);
				if ((stemmed[0] != 0) && (strcmp(withoutDollarSymbol, stemmed) == 0)) {
					if (matchedCnt >= matchedAllocated) {
						matchedAllocated *= 2;
						typed_realloc(ExtentList*, matches, matchedAllocated);
					}
					matches[matchedCnt++] = getPostingListForTerm(i);
				}
			}
		}
		if (matchedCnt <= 1) {
			if (matchedCnt == 0)
				result = new ExtentList_Empty();
			else
				result = matches[0];
			free(matches);
		}
		else
			result = new ExtentList_OR(matches, matchedCnt);
	}
	else {
		// search the hashtable for the given term
		unsigned int hashValue = getHashValue(term);
		unsigned int hashSlot = hashValue % HASHTABLE_SIZE;
		int termID = hashtable[hashSlot];
		while (termID >= 0) {
			if (hashValue == terms[termID].hashValue)
				if (strcmp(term, terms[termID].term) == 0)
					break;
			termID = terms[termID].nextTerm;
		}
		if (termID < 0)
			result = new ExtentList_Empty();
		else if (terms[termID].numberOfPostings == 0)
			result = new ExtentList_Empty();
		else if (terms[termID].numberOfPostings == 1) {
			// special handling for terms with only 1 posting
			offset *postings = typed_malloc(offset, 1);
			*postings = terms[termID].lastPosting;
			return new PostingList(postings, 1, false, true);
		}
		else {
			// this is the general case in which we have to create a SegmentedPostingList instance
			if (terms[termID].numberOfPostings <= TARGET_SEGMENT_SIZE)
				result = getPostingListForTerm(termID);
			else
				result = getSegmentedPostingListForTerm(termID);
		} // end else
	} // end else [neither wildcard nor stemming]

	if (result->getType() == ExtentList::TYPE_EXTENTLIST_OR) {
		ExtentList_OR *orList = (ExtentList_OR*)result;
		if (orList->elemCount == 1) {
			result = orList->elem[0];
			orList->elemCount = 0;
			delete orList;
		}
		else {
			// merge as many sub-lists inside the disjunction as possible
			orList->optimize();
			if (orList->elemCount == 1) {
				result = orList->elem[0];
				orList->elemCount = 0;
				delete orList;
			}
		}
	} // end if (result->getType() == ExtentList::TYPE_EXTENTLIST_OR)
	
	if (mustReleaseReadLock)
		releaseReadLock();
	return result;
} // end of getUpdates(char*)


PostingList * ReallocLexicon::getPostingListForTerm(int termID) {
	int numOfPostings = terms[termID].numberOfPostings;
	offset *result = typed_malloc(offset, numOfPostings + 1);
	if (numOfPostings <= 1)
		result[0] = terms[termID].lastPosting;
	else {
		byte *inputBuffer = terms[termID].postings;
		int chunkPos = 0, shift = 0;
		offset lastOffset = 0;
		for (int i = 0; i < numOfPostings; i++) {
			while (inputBuffer[chunkPos] >= 128) {
				offset b = (inputBuffer[chunkPos++] & 127);
				lastOffset += (b << shift);
				shift += 7;
			}
			offset b = inputBuffer[chunkPos++];
			lastOffset += (b << shift);
			result[i] = lastOffset;
			shift = 0;
		}
	} // end else [numOfPostings > 1]
	return new PostingList(result, numOfPostings, false, true);
} // end of getPostingListForTerm(int)


SegmentedPostingList * ReallocLexicon::getSegmentedPostingListForTerm(int termID) {
	// initialize data for SegmentedPostingList instance
	int segmentCount = 0;
	int segmentsAllocated = 4;
	SPL_OnDiskSegment *segments = typed_malloc(SPL_OnDiskSegment, segmentsAllocated);
	offset outputBuffer[TARGET_SEGMENT_SIZE];

	// initialize internal processing data
	int numOfPostings = terms[termID].numberOfPostings;
	int outPos = 0;
	byte *inputBuffer = terms[termID].postings;
	int chunkPos = 0;
	offset lastOffset = 0;
	int shift = 0;

	for (int i = 0; i < numOfPostings; i++) {
		while (inputBuffer[chunkPos] >= 128) {
			offset b = (inputBuffer[chunkPos++] & 127);
			lastOffset += (b << shift);
			shift += 7;
		}
		offset b = inputBuffer[chunkPos++];
		lastOffset += (b << shift);
		outputBuffer[outPos] = lastOffset;
		shift = 0;
		if (++outPos >= TARGET_SEGMENT_SIZE) {
			if (segmentCount >= segmentsAllocated) {
				segmentsAllocated *= 2;
				typed_realloc(SPL_OnDiskSegment, segments, segmentsAllocated);
			}
			int byteLength;
			byte *compressed = compressVByte(outputBuffer, outPos, &byteLength);
			segments[segmentCount].count = outPos;
			segments[segmentCount].firstPosting = outputBuffer[0];
			segments[segmentCount].lastPosting = outputBuffer[outPos - 1];
			segments[segmentCount].byteLength = byteLength;
			segments[segmentCount].file = new FileFile((char*)compressed, byteLength, false, true);
			segmentCount++;
			outPos = 0;
		}
	} // end for (int i = 0; i < numOfPostings; i++)

	// if there are any data left, compress them as well
	if (outPos > 0) {
		if (segmentCount >= segmentsAllocated) {
			segmentsAllocated++;
			typed_realloc(SPL_OnDiskSegment, segments, segmentsAllocated);
		}
		int byteLength;
		byte *compressed = compressVByte(outputBuffer, outPos, &byteLength);
		segments[segmentCount].count = outPos;
		segments[segmentCount].firstPosting = outputBuffer[0];
		segments[segmentCount].lastPosting = outputBuffer[outPos - 1];
		segments[segmentCount].byteLength = byteLength;
		segments[segmentCount].file = new FileFile((char*)compressed, byteLength, false, true);
		segmentCount++;
	}

	return new SegmentedPostingList(segments, segmentCount);
} // endof getSegmentedPostingListForTerm(int)


IndexIterator * ReallocLexicon::getIterator() {
	return new ReallocLexiconIterator(this);
}


void ReallocLexicon::getClassName(char *target) {
	strcpy(target, "ReallocLexicon");
}


