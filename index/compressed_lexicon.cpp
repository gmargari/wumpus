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
 * Implementation of the CompressedLexicon class.
 *
 * author: Stefan Buettcher
 * created: 2005-01-10
 * changed: 2007-07-13
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "compressed_lexicon.h"
#include "index.h"
#include "index_iterator.h"
#include "index_merger.h"
#include "compressed_lexicon_iterator.h"
#include "segmentedpostinglist.h"
#include "../filesystem/filefile.h"
#include "../misc/all.h"
#include "../query/query.h"
#include "../stemming/stemmer.h"


static const char *LOG_ID = "CompressedLexicon";

const double CompressedLexicon::SLOT_GROWTH_RATE;


CompressedLexicon::CompressedLexicon() {
}


CompressedLexicon::CompressedLexicon(Index *owner, int documentLevelIndexing) {
	this->owner = owner;
	this->documentLevelIndexing = documentLevelIndexing;
	containers = typed_malloc(byte*, MAX_CONTAINER_COUNT);

	// initialize data
	termCount = 0;
	termSlotsAllocated = INITIAL_SLOT_COUNT;
	terms = typed_malloc(CompressedLexiconEntry, termSlotsAllocated);
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = -1;
	containers[0] = (byte*)malloc(CONTAINER_SIZE);
	posInCurrentContainer = 0;
	containerCount = 1;

	// update "occupied memory" information
	memoryOccupied = termSlotsAllocated * sizeof(CompressedLexiconEntry);
	memoryOccupied += HASHTABLE_SIZE * sizeof(int32_t);
	memoryOccupied += MAX_CONTAINER_COUNT * sizeof(byte*);
	memoryOccupied += CONTAINER_SIZE;

	if (documentLevelIndexing > 0) {
		currentDocumentStart = -1;
		usedForDocLevel = 0;
		allocatedForDocLevel = INITIAL_DOC_LEVEL_ARRAY_SIZE;
		termsInCurrentDocument = typed_malloc(int32_t, allocatedForDocLevel);
	}
	else
		termsInCurrentDocument = NULL;
} // end of CompressedLexicon(Index*, char*, int)


CompressedLexicon::~CompressedLexicon() {
	for (int i = 0; i < containerCount; i++)
		free(containers[i]);
	free(containers);
	free(terms);
	if (termsInCurrentDocument != NULL)
		free(termsInCurrentDocument);
} // end of CompressedLexicon()


void CompressedLexicon::clear() {
	LocalLock lock(this);

	// release all resources
	clearDocumentLevelPostings();
	free(terms);
	termCount = 0;
	termSlotsAllocated = INITIAL_SLOT_COUNT;
	terms = typed_malloc(CompressedLexiconEntry, termSlotsAllocated);
	
	// virginize hashtable
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = -1;
	for (int i = 0; i < containerCount; i++)
		free(containers[i]);
	containers[0] = (byte*)malloc(CONTAINER_SIZE);
	containerCount = 1;
	posInCurrentContainer = 0;

	// update "occupied memory" information
	memoryOccupied = termSlotsAllocated * sizeof(CompressedLexiconEntry);
	memoryOccupied += HASHTABLE_SIZE * sizeof(int32_t);
	memoryOccupied += MAX_CONTAINER_COUNT * sizeof(byte*);
	memoryOccupied += CONTAINER_SIZE;

	// update coverage information
	firstPosting = MAX_OFFSET;
	lastPosting = 0;
} // end of clear()


void CompressedLexicon::clear(int threshold) {
	if (threshold <= 1) {
		clear();
		return;
	}

	LocalLock lock(this);
	int oldMemoryOccupied = memoryOccupied;
	clearDocumentLevelPostings();

	// remove all lists that have at least "threshold" postings
	int termsRemoved = 0;
	for (int termID = 0; termID < termCount; termID++) {
		if (terms[termID].numberOfPostings >= threshold) {
			// mark all chunks for this term as free
			int nextChunk = terms[termID].firstChunk;
			while (nextChunk >= 0) {
				byte *inputBuffer =
					&containers[nextChunk >> CONTAINER_SHIFT][nextChunk & (CONTAINER_SIZE - 1)];
				nextChunk = *((int32_t*)inputBuffer);
				*((int32_t*)inputBuffer) = -1;
			} // end while (nextChunk >= 0)

			// update term descriptor
			if (terms[termID].memoryConsumed > 256)
				terms[termID].memoryConsumed = 256;
			terms[termID].numberOfPostings = 0;
			terms[termID].firstChunk = terms[termID].currentChunk = -1;

			// increase counter
			termsRemoved++;
		}
	} // end for (int termID = 0; termID < termCount; termID++)

	if (termsRemoved > 0)
		recompactPostings();

	sprintf(errorMessage, "Flushing long lists. Memory consumption before: %d. After: %d.",
			oldMemoryOccupied, memoryOccupied);
	log(LOG_DEBUG, LOG_ID, errorMessage);
} // end of clear(int)


void CompressedLexicon::recompactPostings() {
	// This method closes all gaps in the containers by means of a two-stage process.
	// I am very proud of this process, and it took me quite a while until I could
	// throw away my first implementation and replace it with this version. :-)

	// Stage 1: For every term in the Lexicon, walk through the list of its chunks
	// and replace the "nextChunk" pointer by the term's ID.
	for (int i = 0; i < termCount; i++) {
		int cur = terms[i].firstChunk;
		while (cur >= 0) {
			int32_t *ptr =
				(int32_t*)&containers[cur >> CONTAINER_SHIFT][cur & (CONTAINER_SIZE - 1)];
			cur = *ptr;
			*ptr = i;
		}
		terms[i].firstChunk = -1;
		terms[i].currentChunk = -1;
	}

	// Stage 2: Walk through the containers; for every chunk encountered, move the
	// chunk to the front, closing the gap in front of the chunk, and put a pointer
	// to the current chunk into the previous one. Use the "currentChunk" pointer
	// in the term descriptor to remember the previous chunk.
	int inContainer = 0, inPos = 0;
	byte *inC = containers[0];
	int outContainer = 0, outPos = 0;
	byte *outC = containers[0];
	while (inContainer < containerCount) {
		if (inPos > CONTAINER_SIZE - 5) {
			inC = containers[++inContainer];
			inPos = 0;
			continue;
		}
		int32_t *ptr = (int32_t*)&inC[inPos];
		int chunkSize = inC[inPos + 4];
		if (chunkSize == 0) {
			// If we encounter a chunk of length 0, we know that this is the end
			// of the current container.
			inC = containers[++inContainer];
			inPos = 0;
		}
		else if (*ptr >= 0) {
			// copy chunk to new position, closing the gap in front of the chunk
			int termID = *ptr;
			*ptr = -1;
			int chunkSize = inC[inPos + 4];
			if (outPos + chunkSize > CONTAINER_SIZE) {
				if (outPos <= CONTAINER_SIZE - 5) {
					*((int32_t*)&outC[outPos]) = -1;
					outC[outPos + 4] = 0;
				}
				outC = containers[++outContainer];
				outPos = 0;
			}
			if ((inContainer != outContainer) || (outPos < inPos - 256))
				memcpy(&outC[outPos], ptr, chunkSize);
			else
				memmove(&outC[outPos], ptr, chunkSize);				

			// update pointers to term's first chunk, term's current chunk, and term's
			// current chunk's "next" pointer
			if (terms[termID].firstChunk < 0)
				terms[termID].firstChunk = (outContainer << CONTAINER_SHIFT) + outPos;
			if (terms[termID].currentChunk >= 0) {
				byte *container = containers[terms[termID].currentChunk >> CONTAINER_SHIFT];
				int32_t *cc =
					(int32_t*)&container[terms[termID].currentChunk & (CONTAINER_SIZE - 1)];
				*cc = (outContainer << CONTAINER_SHIFT) + outPos;
			}
			terms[termID].currentChunk = (outContainer << CONTAINER_SHIFT) + outPos;
			outPos += chunkSize;
		}
		inPos += chunkSize;
	} // end while (inContainer < containerCount)

	// Terminate the container so that we can run this method a second time
	// without getting a segmentation fault.
	if (outPos <= CONTAINER_SIZE - 5) {
		*((int32_t*)&outC[outPos]) = -1;
		outC[outPos + 4] = 0;
	}

	// free all unnecessary containers and update pointer to next free piece of memory
	for (int i = outContainer + 1; i < containerCount; i++) {
		free(containers[i]);
		containers[i] = NULL;
		memoryOccupied -= CONTAINER_SIZE;
	}
	containerCount = outContainer + 1;
	posInCurrentContainer = outPos;
} // end of recompactPostings()


void CompressedLexicon::extendTermsArray() {
	memoryOccupied -= termSlotsAllocated * sizeof(CompressedLexiconEntry);
	termSlotsAllocated = (int)(termCount * SLOT_GROWTH_RATE);
	if (termSlotsAllocated < termCount + INITIAL_SLOT_COUNT)
		termSlotsAllocated = termCount + INITIAL_SLOT_COUNT;
	typed_realloc(CompressedLexiconEntry, terms, termSlotsAllocated);
	memoryOccupied += termSlotsAllocated * sizeof(CompressedLexiconEntry);
} // end of extendTermsArray()


int32_t CompressedLexicon::allocateNewChunk(int size) {
	// It is absolutely mandatory that the size of the chunk to be allocated
	// is smaller than 256, as we use an 8-bit integer to store the chunk size
	// for the given term. If size >= 256, then this is a bug in the program!
	assert(size < 256);
	assert((size & 3) == 0);

	// check whether we have enough free space in the current container
	// to allocate another chunk; if not, start new container
	if (posInCurrentContainer + size > CONTAINER_SIZE) {
		containers[containerCount] = (byte*)malloc(CONTAINER_SIZE);
		containerCount++;
		posInCurrentContainer = 0;
		memoryOccupied += CONTAINER_SIZE;
	}

	// allocate space for new chunk in current container
	int32_t result = (containerCount - 1) * CONTAINER_SIZE + posInCurrentContainer;
	*((int32_t*)&containers[containerCount - 1][posInCurrentContainer]) = -1;
	containers[containerCount - 1][posInCurrentContainer + 4] = (size & 255);
	posInCurrentContainer += size;
	if (posInCurrentContainer <= CONTAINER_SIZE - 5) {
		*((int32_t*)&containers[containerCount - 1][posInCurrentContainer]) = -1;
		containers[containerCount - 1][posInCurrentContainer + 4] = 0;
	}
	return result;
} // end of allocateNewChunk(int)


void CompressedLexicon::addDocumentLevelPostings() {
	if ((documentLevelIndexing <= 0) || (this->currentDocumentStart < 0))
		return;
	char term[2 * MAX_TOKEN_LENGTH];
	offset posting;
	offset currentDocumentStart = this->currentDocumentStart;
	strcpy(term, "<!>");
	if ((currentDocumentStart & DOC_LEVEL_MAX_TF) != 0)
		currentDocumentStart = (currentDocumentStart | DOC_LEVEL_MAX_TF) + 1;
	for (int i = 0; i < usedForDocLevel; i++) {
		int id = termsInCurrentDocument[i];
		assert(terms[id].term[1] != '!');
		posting =
			currentDocumentStart + encodeDocLevelTF(terms[id].postingsInCurrentDocument);
		term[MAX_TOKEN_LENGTH] = 0;
		strcpy(&term[3], terms[id].term);
		if (term[MAX_TOKEN_LENGTH] == 0)
			addPosting(term, posting, getHashValue(term));
	}
} // end of addDocumentLevelPostings()


void CompressedLexicon::clearDocumentLevelPostings() {
	if (documentLevelIndexing <= 0)
		return;
	for (int i = 0; i < usedForDocLevel; i++) {
		int id = termsInCurrentDocument[i];
		terms[id].postingsInCurrentDocument = 0;
	}
	usedForDocLevel = 0;
	if (allocatedForDocLevel > INITIAL_DOC_LEVEL_ARRAY_SIZE) {
		free(termsInCurrentDocument);
		allocatedForDocLevel = INITIAL_DOC_LEVEL_ARRAY_SIZE;
		termsInCurrentDocument = typed_malloc(int32_t, allocatedForDocLevel);
	}
	currentDocumentStart = -1;
} // end of clearDocumentLevelPostings()


int32_t CompressedLexicon::addPosting(char *term, offset posting, unsigned int hashValue) {
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

		terms[termID].firstChunk = -1;
		terms[termID].numberOfPostings = 1;
		terms[termID].lastPosting = posting;
		terms[termID].postingsInCurrentDocument = 0;
		if (term[0] == '<')
			if ((term[1] == '!') || (hashValue == startDocHashValue) || (hashValue == endDocHashValue)) {
				terms[termID].numberOfPostings = 1;
				terms[termID].lastPosting = posting;
				terms[termID].postingsInCurrentDocument = 65535;
				goto skipDocumentLevelInitialization;
			}

		if (documentLevelIndexing >= 2)
			terms[termID].numberOfPostings = 0;
		else {
			terms[termID].numberOfPostings = 1;
			terms[termID].lastPosting = posting;
		}
		terms[termID].postingsInCurrentDocument = 0;

skipDocumentLevelInitialization:

		// set "stemmedForm" according to the situation; apply stemming if
		// STEMMING_LEVEL > 0
		int len = strlen(term);
		if (term[len - 1] == '$')
			terms[termID].stemmedForm = -1;
		else if (terms[termID].postingsInCurrentDocument >= 32768)
			terms[termID].stemmedForm = termID;
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

		if (documentLevelIndexing >= 2) {
			if (terms[termID].postingsInCurrentDocument < 32768)
				goto addPosting_endOfBitgeficke;
			else if ((currentDocumentStart > 0) && (hashValue != startDocHashValue) && (hashValue != endDocHashValue))
				assert(posting < currentDocumentStart + 64);
		}

		bool resetLastPosting = false;

		// we only add more than the first posting if:
		// - we are in STEMMING_LEVEL < 3 (means: we keep non-stemmed terms) or
		// - the term is not stemmable (stemmedForm == termID) or
		// - the term is already the stemmed form (stemmedForm < 0)
		if ((stemmingLevel >= 3) && (terms[termID].stemmedForm >= 0) && (terms[termID].stemmedForm != termID))
			goto beforeAddingPostingForStemmedForm;

#if SUPPORT_APPEND_TAIT
		if (posting == terms[termID].lastPosting)
			resetLastPosting = true;
		else if (posting < terms[termID].lastPosting) {
			// vbyte compression does not allow us to add a posting that is smaller
			// than the previous one; we have to insert a "reset" posting first so
			// that we can encode the incoming posting as a proper delta value
			addPosting(term, terms[termID].lastPosting, hashValue);
			addPosting(term, posting, hashValue);
			return termID;
		}
#else
		if (posting <= terms[termID].lastPosting) {
			snprintf(errorMessage, sizeof(errorMessage),
					"Postings not monotonically increasing: %lld, %lld",
					static_cast<long long>(terms[termID].lastPosting),
					static_cast<long long>(posting));
			log(LOG_DEBUG, LOG_ID, errorMessage);
			goto addPosting_endOfBitgeficke;
		}
#endif

		if (terms[termID].numberOfPostings <= 1) {
			if (terms[termID].numberOfPostings == 0) {
				// we have no postings yet for this guy; this can only happen if it is
				// one of the survivor terms from an earlier part of the text collection;
				// data has already been initialized, so we don't need to do anything here
				terms[termID].lastPosting = posting;
			}
			else {
				// in this case, no chunk has been created yet; so, create the first
				// chunk and move both the first and the new posting into that chunk;
				// make sure that the total size of the chunk (including the 5 control
				// bytes) is a multiple of 4, to keep things word-aligned
				int newChunkSize = (INITIAL_CHUNK_SIZE | 3);
				int chunk = allocateNewChunk(newChunkSize + 5);
				terms[termID].firstChunk = chunk;
				terms[termID].currentChunk = chunk;
				terms[termID].memoryConsumed = newChunkSize;
				byte *chunkData = &containers[chunk >> CONTAINER_SHIFT][chunk & (CONTAINER_SIZE - 1)];
				int posInChunk = 5;
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
				terms[termID].posInCurrentChunk = posInChunk;
				terms[termID].sizeOfCurrentChunk = newChunkSize + 5;
			}
		} // end if (terms[termID].numberOfPostings <= 1)
		else {
			// we already have stuff in the chunks, so just append...
			int posInChunk = terms[termID].posInCurrentChunk;
			int sizeOfChunk = terms[termID].sizeOfCurrentChunk;
			int currentChunk = terms[termID].currentChunk;
			byte *chunkData =
				&containers[currentChunk >> CONTAINER_SHIFT][currentChunk & (CONTAINER_SIZE - 1)];

			// let "value" contain the d-gap with respect to the previous posting
			offset value = posting - terms[termID].lastPosting;
			if (posInChunk <= sizeOfChunk - 8) {
				// if we have enough free space (7 * 8 = 56 bits are enough here, since
				// we cannot have postings larger than this), do the encoding without
				// checking for buffer overflow
				while (value >= 128) {
					chunkData[posInChunk++] = (value & 127) | 128;
					value >>= 7;
				}
				chunkData[posInChunk++] = value;
			}
			else {
				// if less than 56 bits are free, we might have to allocate a new chunk...
				while (true) {
					if (posInChunk >= sizeOfChunk) {
						// create new chunk, based on the total memory consumption of the
						// given term so far
						int newChunk, newChunkSize;
						if (CHUNK_GROWTH_RATE <= 0)
							newChunkSize = INITIAL_CHUNK_SIZE;
						else {
							newChunkSize = terms[termID].memoryConsumed;
							newChunkSize = (newChunkSize  * CHUNK_GROWTH_RATE) >> 5;
							if (newChunkSize < INITIAL_CHUNK_SIZE)
								newChunkSize = INITIAL_CHUNK_SIZE;
						}
						// make sure the total chunk size (including header) is a multiple of 4
						// so that we don't get SIGBUS when accessing the header as an int32_t
						newChunkSize |= 3;
						if (newChunkSize > 247)
							newChunkSize = 247;
						newChunk = allocateNewChunk(newChunkSize + 5);
						*((int32_t*)chunkData) = newChunk;
						chunkData = &containers[newChunk >> CONTAINER_SHIFT][newChunk & (CONTAINER_SIZE - 1)];
						terms[termID].currentChunk = currentChunk = newChunk;
						terms[termID].sizeOfCurrentChunk = sizeOfChunk = (newChunkSize + 5);
						if (terms[termID].memoryConsumed < 60000)
							terms[termID].memoryConsumed += newChunkSize;
						posInChunk = 5;
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
			terms[termID].posInCurrentChunk = posInChunk;
		} // end else [numberOfPostings >= 2]
		terms[termID].lastPosting = posting;
		terms[termID].numberOfPostings++;

#if SUPPORT_APPEND_TAIT
		if (resetLastPosting)
			terms[termID].lastPosting = 0;
#endif

beforeAddingPostingForStemmedForm:

		// add posting for stemmed form, if desired
		int stemmedForm = terms[termID].stemmedForm;
		if ((stemmedForm >= 0) && (stemmedForm != termID))
			addPosting(terms[stemmedForm].term, posting, terms[stemmedForm].hashValue);
	} // end else [termID >= 0]

addPosting_endOfBitgeficke:

	// if document-level indexing has been enabled, we store additional postings
	// in the index that tell us how many occurrences of a given term we have in
	// a given document
	if (documentLevelIndexing > 0) {
		if (hashValue == startDocHashValue) {
			if (strcmp(term, START_OF_DOCUMENT_TAG) == 0) {
				clearDocumentLevelPostings();
				currentDocumentStart = posting;
				terms[termID].postingsInCurrentDocument = 65535;
			}
		}
		else if (hashValue == endDocHashValue) {
			if (strcmp(term, END_OF_DOCUMENT_TAG) == 0) {
				terms[termID].postingsInCurrentDocument = 65535;
				if ((currentDocumentStart & DOC_LEVEL_MAX_TF) == 0) {
					if (posting > currentDocumentStart + DOC_LEVEL_MAX_TF/2 + 1)
						addDocumentLevelPostings();
				}
				else {
					if (posting > (currentDocumentStart | DOC_LEVEL_MAX_TF) + DOC_LEVEL_MAX_TF/2 + 2)
						addDocumentLevelPostings();
				}
				clearDocumentLevelPostings();
			}
		}
		else {
			if (terms[termID].postingsInCurrentDocument == 0) {
				if (allocatedForDocLevel <= usedForDocLevel) {
					allocatedForDocLevel *= 2;
					typed_realloc(int32_t, termsInCurrentDocument, allocatedForDocLevel);
				}
				termsInCurrentDocument[usedForDocLevel++] = termID;
			}
			if (terms[termID].postingsInCurrentDocument < 9999)
				terms[termID].postingsInCurrentDocument++;
		}
	} // end if (DOCUMENT_LEVEL_INDEXING)

	return termID;
} // end of addPosting(char*, offset, unsigned int)


void CompressedLexicon::addPostings(char **terms, offset *postings, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	for (int i = 0; i < count; i++)
		CompressedLexicon::addPosting(terms[i], postings[i], getHashValue(terms[i]));
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(char**, offset*, int)


void CompressedLexicon::addPostings(char *term, offset *postings, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	unsigned int hashValue = getHashValue(term);
	for (int i = 0; i < count; i++)
		CompressedLexicon::addPosting(term, postings[i], hashValue);
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(char*, offset*, int)


void CompressedLexicon::addPostings(InputToken *terms, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	for (int i = 0; i < count; i++)
		CompressedLexicon::addPosting((char*)terms[i].token, terms[i].posting, terms[i].hashValue);
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(InputToken*, int)


void CompressedLexicon::addPostingsToCompactIndex(CompactIndex *target, char *term, int termID) {
	int postingCount = terms[termID].numberOfPostings;

	// if we have only one posting, it is stored directly in the "lastPosting" variable
	if (postingCount == 1)
		target->addPostings(term, &terms[termID].lastPosting, 1);
	if (postingCount <= 1)
		return;

#if SUPPORT_APPEND_TAIT
	PostingList *list = getPostingListForTerm(termID);
	target->addPostings(term, list->postings, list->length);
	delete list;
	return;
#endif

	// for more than one posting, we have to traverse the linked list for the given term
	static const int BUFFER_SIZE = MAX_SEGMENT_SIZE * 6;
	byte outputBuffer[BUFFER_SIZE];
	int outputBufferPos = 16;

	offset currentPosting = 0;
	int postingsTransferred = 0;
	int32_t nextChunk = terms[termID].firstChunk;

	while ((postingsTransferred < postingCount) || (nextChunk >= 0)) {

		while ((nextChunk >= 0) && (outputBufferPos + 256 < BUFFER_SIZE)) {
			byte *inputBuffer =
				&containers[nextChunk >> CONTAINER_SHIFT][nextChunk & (CONTAINER_SIZE - 1)];
			nextChunk = *((int32_t*)inputBuffer);
			int chunkSize;
			if (nextChunk < 0)
				chunkSize = terms[termID].posInCurrentChunk - 5;
			else
				chunkSize = inputBuffer[4] - 5;
			memcpy(&outputBuffer[outputBufferPos], &inputBuffer[5], chunkSize);
			outputBufferPos += chunkSize;
		} // end while ((nextChunk >= 0) && (outputBufferPos + 256 < BUFFER_SIZE))

		offset firstPosting, lastPosting;
		int segmentLength, lengthOfFirstPosting;
		int pos = 16;

		if (postingsTransferred + MAX_SEGMENT_SIZE >= postingCount) {
			// if the remainder of the list is shorter than MAX_SEGMENT_SIZE, things
			// are easy: adjust the first posting and prepend the compression header
			segmentLength = postingCount - postingsTransferred;

			// compute new first posting and determine last posting
			lengthOfFirstPosting = decodeVByteOffset(&firstPosting, &outputBuffer[pos]);
			pos += lengthOfFirstPosting;
			firstPosting = currentPosting = (firstPosting + currentPosting);
			for (int i = 1; i < segmentLength; i++) {
				offset delta;
				pos += decodeVByteOffset(&delta, &outputBuffer[pos]);
				currentPosting += delta;
			}
			lastPosting = currentPosting;
			assert(pos == outputBufferPos);
			assert(lastPosting == terms[termID].lastPosting);
		}
		else {
			// if the remainder is longer, things are slightly more complicated, as
			// we need to obtain the value of the last posting in the list in order
			// to pass it to the CompactIndex
			segmentLength = TARGET_SEGMENT_SIZE;

			// compute new first posting and determine last posting
			lengthOfFirstPosting = decodeVByteOffset(&firstPosting, &outputBuffer[pos]);
			pos += lengthOfFirstPosting;
			firstPosting = currentPosting = (firstPosting + currentPosting);
			for (int i = 1; i < segmentLength; i++) {
				offset delta;
				pos += decodeVByteOffset(&delta, &outputBuffer[pos]);
				currentPosting += delta;
			}
			lastPosting = currentPosting;
		} // end else [postingsTransferred + MAX_SEGMENT_SIZE < postingCount]

		// adjust compressed list header and representation of first posting
		int lengthOfNewFirstPosting = getVByteLength(firstPosting);
		int lengthOfLength = getVByteLength(segmentLength);
		int newPos = 16 + lengthOfFirstPosting - lengthOfNewFirstPosting;
		encodeVByteOffset(firstPosting, &outputBuffer[newPos]);
		newPos -= lengthOfLength;
		encodeVByte32(segmentLength, &outputBuffer[newPos]);
		outputBuffer[--newPos] = COMPRESSION_VBYTE;

		// transfer compressed postings to target index
		target->addPostings(term, &outputBuffer[newPos], pos - newPos,
				segmentLength, firstPosting, lastPosting);
		postingsTransferred += segmentLength;

		if (postingsTransferred < postingCount) {
			memmove(&outputBuffer[16], &outputBuffer[pos], outputBufferPos - pos);
			outputBufferPos -= (pos - 16);
		}

	} // end while ((postingsTransferred < postingCount) || (nextChunk >= 0))

} // end of addPostingsToCompactIndex(CompactIndex*, char*, int)


void CompressedLexicon::createCompactIndex(const char *fileName) {
	assert(termCount > 0);

	bool mustReleaseReadLock = getReadLock();

	// add document-level postings if appropriate
	clearDocumentLevelPostings();

	int stemmingLevel = owner->STEMMING_LEVEL;
	int documentLevelIndexing = this->documentLevelIndexing;
	int32_t *sortedTerms = sortTerms();
	CompactIndex *target = CompactIndex::getIndex(owner, fileName, true);

	for (int i = 0; i < termCount; i++) {
		int termID = sortedTerms[i];

		// if requested, discard everything that is not document-level information
		if (documentLevelIndexing >= 2)
			if (terms[termID].postingsInCurrentDocument < 32768)
				continue;

		// if requested, discard all unstemmed-but-stemmable term information
		if (stemmingLevel >= 3)
			if ((terms[termID].stemmedForm >= 0) && (terms[termID].stemmedForm != termID))
				continue;

		addPostingsToCompactIndex(target, terms[termID].term, termID);
	} // end for (int i = 0; i < termCount; i++)

	free(sortedTerms);

	delete target;
	if (mustReleaseReadLock)
		releaseReadLock();
} // end of createCompactIndex(char*)


void CompressedLexicon::mergeWithExisting(
			IndexIterator **iterators, int iteratorCount, char *outputIndex) {
	if (iterators == NULL) {
		createCompactIndex(outputIndex);
		return;
	}

	bool mustReleaseReadLock = getReadLock();

	// add document-level postings if appropriate
	clearDocumentLevelPostings();

	IndexIterator **newIterators = typed_malloc(IndexIterator*, iteratorCount + 1);
	for (int i = 0; i < iteratorCount; i++)
		newIterators[i] = iterators[i];
	newIterators[iteratorCount] = new CompressedLexiconIterator(this);
	free(iterators);
	iterators = newIterators;
	iteratorCount++;

	IndexMerger::mergeIndices(owner, outputIndex, iterators, iteratorCount);

	if (mustReleaseReadLock)
		releaseReadLock();
} // end of mergeWithExisting(...)


void CompressedLexicon::mergeWithExisting(
			IndexIterator **iterators, int iteratorCount, char *outputIndex, ExtentList *visible) {
	bool mustReleaseReadLock = getReadLock();

	// add document-level postings if appropriate
	clearDocumentLevelPostings();

	IndexIterator **newIterators = typed_malloc(IndexIterator*, iteratorCount + 1);
	for (int i = 0; i < iteratorCount; i++)
		newIterators[i] = iterators[i];
	newIterators[iteratorCount] = new CompressedLexiconIterator(this);
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
	CompressedLexiconEntry *x = (CompressedLexiconEntry*)a;
	CompressedLexiconEntry *y = (CompressedLexiconEntry*)b;
	return strcmp(x->term, y->term);
}


void CompressedLexicon::selectionSort(CompressedLexiconEntry *terms,
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
} // end of selectionSort(CompressedLexiconEntry*, int32_t*, int)


void CompressedLexicon::mergeSort(CompressedLexiconEntry *terms, int32_t *idArray,
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
} // end of mergeSort(CompressedLexiconEntry*, int32_t*, int32_t*, int)


void CompressedLexicon::hybridBucketSort(CompressedLexiconEntry *terms, int32_t *idArray, int count) {
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


int32_t * CompressedLexicon::sortTerms() {
	int32_t *result = typed_malloc(int32_t, termCount);
	for (int i = 0; i < termCount; i++)
		result[i] = i;
	hybridBucketSort(terms, result, termCount);
	return result;
} // end of sortTerms()


ExtentList * CompressedLexicon::getUpdates(const char *term) {
	LocalLock lock(this);
	ExtentList *result = NULL;

	bool isDocumentLevel = startsWith(term, "<!>");

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
		else {
#if SUPPORT_APPEND_TAIT
			result = getPostingListForTerm(termID);
#else
			if (terms[termID].numberOfPostings <= 64)
				result = getPostingListForTerm(termID);
			else
				result = getSegmentedPostingListForTerm(termID);
#endif
		}
	} // end else [neither wildcard nor stemming]

	if (result->getType() == ExtentList::TYPE_EXTENTLIST_OR) {
		ExtentList_OR *orList = (ExtentList_OR*)result;
		if (orList->elemCount == 1) {
			result = orList->elem[0];
			orList->elemCount = 0;
			delete orList;
		}
		else if (isDocumentLevel) {
			// merge document-level lists into one big list representing their disjunction
			result = ExtentList::mergeDocumentLevelLists(orList->elem, orList->elemCount);
			orList->elemCount = 0;
			orList->elem = NULL;
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
	
	return result;
} // end of getUpdates(char*)


PostingList * CompressedLexicon::getPostingListForTerm(int termID) {
	int numOfPostings = terms[termID].numberOfPostings;
	int outPos = 0;
	offset *result = typed_malloc(offset, numOfPostings + 1);
	if (numOfPostings <= 1) {
		result[0] = terms[termID].lastPosting;
		outPos = numOfPostings;
	}
	else {
		offset lastOffset = 0;
		int inputChunk = -1;
		byte *inputBuffer = NULL;
		int nextChunk = terms[termID].firstChunk;
		int chunkSize = 0;
		int chunkPos = 0;
		int shift = 0;
		while (true) {
			while (chunkPos < chunkSize - 6) {
				while (inputBuffer[chunkPos] >= 128) {
					offset b = (inputBuffer[chunkPos++] & 127);
					lastOffset += (b << shift);
					shift += 7;
				}
				offset b = inputBuffer[chunkPos++];
				lastOffset += (b << shift);
				result[outPos++] = lastOffset;
				shift = 0;
			}
			if (chunkPos >= chunkSize) {
				inputChunk = nextChunk;
				if (inputChunk < 0)
					break;
				inputBuffer =
					&containers[inputChunk >> CONTAINER_SHIFT][inputChunk & (CONTAINER_SIZE - 1)];
				nextChunk = *((int32_t*)inputBuffer);
				chunkSize = inputBuffer[4];
				if (nextChunk < 0)
					chunkSize = terms[termID].posInCurrentChunk;
				chunkPos = 5;
			}
			if (inputBuffer[chunkPos] < 128) {
				offset b = inputBuffer[chunkPos++];
				lastOffset += (b << shift);
				result[outPos++] = lastOffset;
				shift = 0;
			}
			else {
				offset b = (inputBuffer[chunkPos++] & 127);
				lastOffset += (b << shift);
				shift += 7;
			}
		} // end while (true)
	} // end else [numOfPostings > 1]
	assert(outPos == terms[termID].numberOfPostings);

#if SUPPORT_APPEND_TAIT
	// If we have support for append operations, then we need to be a bit careful
	// here: Find "reset" postings and adjust all values accordingly.
	int newOutPos = 1;
	for (int i = 1; i < outPos; i++) {
		offset delta = result[i] - result[i - 1];
		if (delta > 0)
			result[newOutPos] = result[newOutPos - 1] + delta;
		else {
			i++;
			result[newOutPos] = result[i] - result[i - 1];
		}
		newOutPos++;
	}
	if (newOutPos < outPos) {
		outPos = newOutPos;
		sortOffsetsAscending(result, outPos);
	}
#endif

	return new PostingList(result, outPos, false, true);
} // end of getPostingListForTerm(int)


SegmentedPostingList * CompressedLexicon::getSegmentedPostingListForTerm(int termID) {
	// initialize data for SegmentedPostingList instance
	int segmentCount = 0;
	int segmentsAllocated = 4;
	SPL_OnDiskSegment *segments = typed_malloc(SPL_OnDiskSegment, segmentsAllocated);
	offset outputBuffer[TARGET_SEGMENT_SIZE];

	// initialize internal processing data
	int outPos = 0;
	offset lastOffset = 0;
	int inputChunk = -1;
	byte *inputBuffer = NULL;
	int nextChunk = terms[termID].firstChunk;
	int chunkSize = 0;
	int chunkPos = 0;
	int shift = 0;

	do {
		if (chunkPos >= chunkSize) {
			inputChunk = nextChunk;
			if (inputChunk < 0)
				break;
			inputBuffer =
				&containers[inputChunk >> CONTAINER_SHIFT][inputChunk & (CONTAINER_SIZE - 1)];
			nextChunk = *((int32_t*)inputBuffer);
			chunkSize = inputBuffer[4];
			if (nextChunk < 0)
				chunkSize = terms[termID].posInCurrentChunk;
			chunkPos = 5;
		}
		if (inputBuffer[chunkPos] < 128) {
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
			} // end if (outPos >= TARGET_SEGMENT_SIZE)
		}
		else {
			offset b = (inputBuffer[chunkPos++] & 127);
			lastOffset += (b << shift);
			shift += 7;
		}
	} while (inputChunk >= 0);

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


IndexIterator * CompressedLexicon::getIterator() {
	return new CompressedLexiconIterator(this);
}


void CompressedLexicon::getClassName(char *target) {
	strcpy(target, "CompressedLexicon");
}


