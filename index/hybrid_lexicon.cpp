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
 * Implementation of the HybridLexicon class. This implementation is purely
 * experimental and should not be used. It is merely a proof of concept in order
 * to show that the file system can be used as the storage layer for a text
 * retrieval system.
 *
 * author: Stefan Buettcher
 * created: 2005-07-05
 * changed: 2007-07-22
 **/


#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "hybrid_lexicon.h"
#include "compressed_lexicon.h"
#include "compressed_lexicon_iterator.h"
#include "index.h"
#include "multiple_index_iterator.h"
#include "postinglist_in_file.h"
#include "../misc/all.h"


static const char *LOG_ID = "HybridLexicon";

const int HybridLexicon::MAX_COMPACTINDEX_COUNT;


HybridLexicon::HybridLexicon(Index *owner, int documentLevelIndexing) :
		CompressedLexicon(owner, documentLevelIndexing) {
	realMemoryConsumption = memoryOccupied;
	memoryOccupied = 0;
	getConfigurationInt("MAX_UPDATE_SPACE", &maxMemoryConsumption, Index::DEFAULT_MAX_UPDATE_SPACE);
	if (maxMemoryConsumption < 16 * 1024 * 1024)
		maxMemoryConsumption = 16 * 1024 * 1024;

	// for all possible subindex IDs, create CompactIndex instance if file
	// exists on disk
	maxIndexID = -1;
	char *ciFileName = evaluateRelativePathName(owner->directory, "index.short.XXX");
	for (int i = 0; i < MAX_COMPACTINDEX_COUNT; i++) {
		sprintf(&ciFileName[strlen(ciFileName) - 3], "%03d", i);
		struct stat buf;
		if (stat(ciFileName, &buf) == 0) {
			compactIndex[i] = CompactIndex::getIndex(owner, ciFileName, false);
			maxIndexID = i;
		}
		else
			compactIndex[i] = NULL;
	}
	free(ciFileName);

	longListIndex = InPlaceIndex::getIndex(owner, owner->directory);
	lastPartialFlushWasSuccessful = true;
	durationOfLastMerge = 0;
} // end of HybridLexicon(Index*, char*, int)


HybridLexicon::~HybridLexicon() {
	memoryOccupied = realMemoryConsumption;
	if (termCount > 0) {
		flushPostingsToDisk();
		clear();
	}
	for (int i = 0; i < MAX_COMPACTINDEX_COUNT; i++) {
		if (compactIndex[i] != NULL) {
			delete compactIndex[i];
			compactIndex[i] = NULL;
		}
	}
	delete longListIndex;
} // end of ~HybridLexicon()


void HybridLexicon::clear() {
	memoryOccupied = realMemoryConsumption;
	CompressedLexicon::clear();
	realMemoryConsumption = memoryOccupied;
} // end of clear()


void HybridLexicon::clear(int threshold) {
	memoryOccupied = realMemoryConsumption;
	CompressedLexicon::clear(threshold);
	realMemoryConsumption = memoryOccupied;
} // end of clear(int)


void HybridLexicon::addPostings(char **terms, offset *postings, int count) {
	memoryOccupied = realMemoryConsumption;
	CompressedLexicon::addPostings(terms, postings, count);
	realMemoryConsumption = memoryOccupied;
	if (memoryOccupied > maxMemoryConsumption)
		partialFlush();
	realMemoryConsumption = memoryOccupied;
	memoryOccupied = 0;
} // end of addPostings(char**, offset*, int)


void HybridLexicon::addPostings(char *term, offset *postings, int count) {
	memoryOccupied = realMemoryConsumption;
	CompressedLexicon::addPostings(term, postings, count);
	realMemoryConsumption = memoryOccupied;
	if (memoryOccupied > maxMemoryConsumption)
		partialFlush();
	realMemoryConsumption = memoryOccupied;
	memoryOccupied = 0;
} // end of addPostings(char*, offset*, int)


void HybridLexicon::addPostings(InputToken *terms, int count) {
	memoryOccupied = realMemoryConsumption;
	CompressedLexicon::addPostings(terms, count);
	realMemoryConsumption = memoryOccupied;
	if (memoryOccupied > maxMemoryConsumption)
		partialFlush();
	realMemoryConsumption = memoryOccupied;
	memoryOccupied = 0;
} // end of addPostings(InputToken*, int)


void HybridLexicon::partialFlush() {
	// obtain and interpret PARTIAL_FLUSH configuration variable; we accept
	// "auto", boolean, or integer thresholds
	static const int NO_PF = 999999;
	char pfString[MAX_CONFIG_VALUE_LENGTH + 1];
	int pfThreshold = NO_PF;
	if (getConfigurationValue("PARTIAL_FLUSH", pfString)) {
		int value;
		if ((strcasecmp(pfString, "AUTO") == 0) || (strcasecmp(pfString, "TRUE") == 0))
			pfThreshold = -1;
		else if (sscanf(pfString, "%d", &value) == 1)
			if (value >= 1)
				pfThreshold = MIN(value, 60000);
	}

	if ((pfThreshold != NO_PF) && (lastPartialFlushWasSuccessful)) {
		if (pfThreshold < 0) {
			// Auto-tune partial flushing threshold based on performance seen during the
			// last merge operation; the optimization step is based on the assumption
			// that the savings we get from flushing a single list can be approximated as:
			//
			//   listSize / realMemoryConsumption * durationOfLastMerge
			//
			// Under this assumption, we flush a list if the expected savings are less
			// than the expected cost of a random disk access (in-place update), assumed
			// to be 30 ms.
			static const double DISK_SEEK_LATENCY = 0.030;  // 30 ms
			if (durationOfLastMerge <= 0) {
				pfThreshold = 60000;
				sprintf(errorMessage, "No merge operation yet. pfThreshold: %d bytes.\n", pfThreshold);
			}
			else {
				pfThreshold = (int)(realMemoryConsumption * DISK_SEEK_LATENCY / durationOfLastMerge);
				sprintf(errorMessage, "Last merge: %.1lf seconds. Optimal pfThreshold: %d bytes.\n",
						durationOfLastMerge, pfThreshold);
			}
			log(LOG_DEBUG, LOG_ID, errorMessage);
		} // end if (pfThreshold < 0)

		pfThreshold = MIN(pfThreshold, 60000);
		sprintf(errorMessage, "Flushing long lists to disk. pfThreshold: %d bytes.", pfThreshold);
		log(LOG_DEBUG, LOG_ID, errorMessage);

		// perform partial flush
		flushLongListsToDisk(pfThreshold);
		longListIndex->finishUpdate();

		snprintf(errorMessage, sizeof(errorMessage),
				"Memory consumption before: %d KB. After: %d KB.",
				realMemoryConsumption / 1024, memoryOccupied / 1024);
		log(LOG_DEBUG, LOG_ID, errorMessage);
		lastPartialFlushWasSuccessful = (memoryOccupied < maxMemoryConsumption * 0.85);
	}
	else {
		log(LOG_DEBUG, LOG_ID, "Flushing entire in-memory index to disk.");
		double startTime = getCurrentTime();

		// transfer entire index to disk
		flushPostingsToDisk();
		clear();
		longListIndex->finishUpdate();

		durationOfLastMerge = getCurrentTime() - startTime;
		if (durationOfLastMerge < 0)
			durationOfLastMerge += SECONDS_PER_DAY;
		lastPartialFlushWasSuccessful = true;
	}
} // end of partialFlush()


void HybridLexicon::flushPostingsToDisk() {
	assert(termCount > 0);
	bool mustReleaseReadLock = getReadLock();
	char mergeStrategy[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue("UPDATE_STRATEGY", mergeStrategy))
		strcpy(mergeStrategy, "IMMEDIATE_MERGE");

	clearDocumentLevelPostings();

	char *longLists = longListIndex->getTermSequence();
	char *nextLongList = longLists;

	int stemmingLevel = owner->STEMMING_LEVEL;
	int documentLevelIndexing = this->documentLevelIndexing;
	int longListThreshold;
	getConfigurationInt("LONG_LIST_THRESHOLD", &longListThreshold, 16384);
	offset *outputBuffer = typed_malloc(offset, longListThreshold);

	// create IndexIterator instance reading from existing on-disk indices and
	// from the Lexicon (use MultipleIndexIterator)
	char *ciFileName = evaluateRelativePathName(owner->directory, "index.short.XXX");
	IndexIterator *iterator = new CompressedLexiconIterator(this);
	IndexIterator **iterators = typed_malloc(IndexIterator*, MAX_COMPACTINDEX_COUNT + 2);
	int firstFree = 0, iteratorCount = 0;
	int subIndexCount = 0;
	CompactIndex *target;

	for (int i = 0; i < MAX_COMPACTINDEX_COUNT; i++)
		if (compactIndex[i] != NULL)
			subIndexCount++;

	if (strcasecmp(mergeStrategy, "LOG_MERGE") == 0) {
		while (compactIndex[firstFree] != NULL)
			firstFree++;
		for (int i = firstFree - 1; i >= 0; i--) {
			if (compactIndex[i] != NULL) {
				sprintf(&ciFileName[strlen(ciFileName) - 3], "%03d", i);
				iterators[iteratorCount++] =
					CompactIndex::getIterator(ciFileName, CompactIndex::DEFAULT_MERGE_BUFFER_PER_INDEX);
			}
		}
		sprintf(&ciFileName[strlen(ciFileName) - 3], "%03d", firstFree);
		target = CompactIndex::getIndex(owner, ciFileName, true);
	}
	else if (strcasecmp(mergeStrategy, "SQRT_MERGE") == 0) {
		double combinedSize = memoryOccupied * 1.0 / maxMemoryConsumption;
		if (compactIndex[0] != NULL) {
			combinedSize += compactIndex[0]->getByteSize() * 1.0 / maxMemoryConsumption;
			if (compactIndex[1] != NULL)
				if (combinedSize > sqrt(compactIndex[1]->getByteSize() * 1.0 / maxMemoryConsumption)) {
					sprintf(&ciFileName[strlen(ciFileName) - 3], "001");
					iterators[iteratorCount++] =
						CompactIndex::getIterator(ciFileName, CompactIndex::DEFAULT_MERGE_BUFFER_PER_INDEX);
				}
			sprintf(&ciFileName[strlen(ciFileName) - 3], "000");
			iterators[iteratorCount++] =
				CompactIndex::getIterator(ciFileName, CompactIndex::DEFAULT_MERGE_BUFFER_PER_INDEX);
		}
		sprintf(&ciFileName[strlen(ciFileName) - 3], "%03d", 999);
		target = CompactIndex::getIndex(owner, ciFileName, true);
	}
	else {
		for (int i = 0; i < MAX_COMPACTINDEX_COUNT; i++)
			if (compactIndex[i] != NULL) {
				sprintf(&ciFileName[strlen(ciFileName) - 3], "%03d", i);
				iterators[iteratorCount++] =
					CompactIndex::getIterator(ciFileName, CompactIndex::DEFAULT_MERGE_BUFFER_PER_INDEX);
			}
		sprintf(&ciFileName[strlen(ciFileName) - 3], "%03d", 999);
		target = CompactIndex::getIndex(owner, ciFileName, true);
		maxIndexID = -1;
	}
	if (iteratorCount == 0) {
		free(iterators);
		iterators = NULL;
		iteratorCount++;
	}
	else {
		iterators[iteratorCount++] = iterator;
		iterator = new MultipleIndexIterator(iterators, iteratorCount);
	}
	free(ciFileName);

	char *nextTerm;
	while ((nextTerm = iterator->getNextTerm()) != NULL) {
		// skip over long lists for which there is nothing in the mergeable index
		while ((nextLongList[0] != 0) && (strcmp(nextLongList, nextTerm) < 0))
			nextLongList = &nextLongList[strlen(nextLongList) + 1];

		int outputBufferPos = 0, bytesForCurrentTerm = 0;

		// check whether this list has to be put in a separate file
		bool isLongList = false;
		char term[MAX_TOKEN_LENGTH * 2];
		strcpy(term, nextTerm);
		if (strcmp(nextLongList, nextTerm) == 0)
			isLongList = true;
		else if (iteratorCount == subIndexCount + 1) {
			// process short list and change its status to "long" if appropriate; only
			// do this if all sub-indices are involved in the current merge process;
			// this way, we avoid expensive list relocations later on
			do {
				if (strcmp(nextTerm, term) != 0)
					break;
				PostingListSegmentHeader *header = iterator->getNextListHeader();
				bytesForCurrentTerm += header->byteLength;
				if (bytesForCurrentTerm >= longListThreshold) {
					isLongList = true;
					break;
				}
				int length;
				iterator->getNextListUncompressed(&length, &outputBuffer[outputBufferPos]);
				outputBufferPos += length;
			} while ((nextTerm = iterator->getNextTerm()) != NULL);
		} // end else if (iteratorCount == subIndexCount + 1)
		else if (!isLongList) {
			// this is a short list; we will not change its status to "long"
			// => do a plain copy operation
			outputBufferPos = 0;
			do {
				if (strcmp(nextTerm, term) != 0)
					break;
				int length;
				PostingListSegmentHeader *header = iterator->getNextListHeader();
				if (outputBufferPos + header->postingCount > longListThreshold) {
					if (outputBufferPos > 0)
						target->addPostings(term, outputBuffer, outputBufferPos);
					if (header->postingCount > longListThreshold/3) {
						offset *postings = iterator->getNextListUncompressed(&length, NULL);
						target->addPostings(term, postings, length);
						free(postings);
						outputBufferPos = 0;
					}
					else {
						iterator->getNextListUncompressed(&length, outputBuffer);
						outputBufferPos = length;
					}
				}
				else {
					iterator->getNextListUncompressed(&length, &outputBuffer[outputBufferPos]);
					outputBufferPos += length;
				}
			} while ((nextTerm = iterator->getNextTerm()) != NULL);
			if (outputBufferPos > 0) {
				target->addPostings(term, outputBuffer, outputBufferPos);
				outputBufferPos = 0;
			}
		} // end else if (!isLongList)

		if (isLongList) {
			// long lists go into the in-place index
			if (outputBufferPos > 0) {
				longListIndex->addPostings(term, outputBuffer, outputBufferPos);
				outputBufferPos = 0;
			}
			while (nextTerm != NULL) {
				if (strcmp(nextTerm, term) != 0)
					break;
				PostingListSegmentHeader header = *(iterator->getNextListHeader());
				int cnt, size;
				byte *postings = iterator->getNextListCompressed(&cnt, &size, NULL);
				assert(cnt == header.postingCount);
				assert(size == header.byteLength);
				longListIndex->addPostings(
						term, postings, size, cnt, header.firstElement, header.lastElement);
				free(postings);
				nextTerm = iterator->getNextTerm();
			} // end while (nextTerm != NULL)
		}
		else if (outputBufferPos > 0) {
			// this is a short list: put it into the index for all the short lists
			target->addPostings(term, outputBuffer, outputBufferPos);
		}
	} // end while ((nextTerm = iterator->getNextTerm()) != NULL)

	// index iterator is exhausted: release all resources
	free(outputBuffer);
	free(longLists);
	delete iterator;

	if (strcasecmp(mergeStrategy, "LOG_MERGE") == 0) {
		for (int i = 0; i < firstFree; i++)
			if (compactIndex[i] != NULL) {
				char *fileName = compactIndex[i]->getFileName();
				delete compactIndex[i];
				compactIndex[i] = NULL;
				unlink(fileName);
				free(fileName);
			}
		char *fileName = target->getFileName();
		delete target;
		compactIndex[firstFree] = CompactIndex::getIndex(owner, fileName, false);
		free(fileName);
		if (firstFree > maxIndexID)
			maxIndexID = firstFree;
	}
	else if (strcasecmp(mergeStrategy, "SQRT_MERGE") == 0) {
		char *fileName = NULL;
		char *oldFileName, *newFileName;
		switch (iteratorCount) {
			case 1:
				assert(subIndexCount <= 1);
				if (subIndexCount == 0) {
					oldFileName = target->getFileName();
					newFileName = evaluateRelativePathName(owner->directory, "index.short.001");
					delete target;
					rename(oldFileName, newFileName);
					compactIndex[1] = CompactIndex::getIndex(owner, newFileName, false);
					free(oldFileName);
					free(newFileName);
					maxIndexID = 1;
				}
				if (subIndexCount == 1) {
					oldFileName = target->getFileName();
					newFileName = evaluateRelativePathName(owner->directory, "index.short.000");
					delete target;
					rename(oldFileName, newFileName);
					compactIndex[0] = CompactIndex::getIndex(owner, newFileName, false);
					free(oldFileName);
					free(newFileName);
				}
				break;
			case 2:
				assert(compactIndex[0] != NULL);
				fileName = compactIndex[0]->getFileName();
				delete compactIndex[0];
				unlink(fileName);
				oldFileName = target->getFileName();
				delete target;
				rename(oldFileName, fileName);
				compactIndex[0] = CompactIndex::getIndex(owner, fileName, false);
				free(oldFileName);
				free(fileName);
				break;
			case 3:
				for (int i = 0; i < MAX_COMPACTINDEX_COUNT; i++)
					if (compactIndex[i] != NULL) {
						fileName = compactIndex[i]->getFileName();
						delete compactIndex[i];
						compactIndex[i] = NULL;
						unlink(fileName);
						free(fileName);
					}
				oldFileName = target->getFileName();
				newFileName = evaluateRelativePathName(owner->directory, "index.short.001");
				delete target;
				rename(oldFileName, newFileName);
				compactIndex[1] = CompactIndex::getIndex(owner, newFileName, false);
				free(oldFileName);
				free(newFileName);
				break;
			default:
				assert("This should never happen!" == NULL);
		}
	}
	else {
		for (int i = 0; i < MAX_COMPACTINDEX_COUNT; i++)
			if (compactIndex[i] != NULL) {
				char *fileName = compactIndex[i]->getFileName();
				delete compactIndex[i];
				compactIndex[i] = NULL;
				unlink(fileName);
				free(fileName);
			}
		char *oldFileName = target->getFileName();
		char *newFileName = evaluateRelativePathName(owner->directory, "index.short.000");
		delete target;
		rename(oldFileName, newFileName);
		compactIndex[0] = CompactIndex::getIndex(owner, newFileName, false);
		free(oldFileName);
		free(newFileName);
		maxIndexID = 0;
	}

	if (mustReleaseReadLock)
		releaseReadLock();
} // end of flushPostingsToDisk()


void HybridLexicon::flushLongListsToDisk(int minSize) {
	// obtain sequence of null-terminated strings; sequence is terminated
	// by string of length 0
	char *longLists = longListIndex->getTermSequence();
	char *longList = longLists;

	// traverse sequence of "long lists" (lists residing inside the in-place index);
	// flush all long lists that consume more than "minSize" bytes to disk
	int termsFlushed = 0;
	while (*longList != 0) {
		unsigned int hashValue = getHashValue(longList);
		int termID = hashtable[hashValue % HASHTABLE_SIZE];
		while (termID >= 0) {
			if (terms[termID].hashValue == hashValue)
				if (strcmp(terms[termID].term, longList) == 0) {
					if (terms[termID].memoryConsumed >= minSize) {
						termsFlushed++;
						flushLongListToDisk(termID);
					}
					break;
				}
			termID = terms[termID].nextTerm;
		}
		longList = &longList[strlen(longList) + 1];
	}

	// and now we have to clean up after ourselves
	free(longLists);
	if (termsFlushed > 0)
		recompactPostings();
} // end of flushLongListsToDisk()


void HybridLexicon::flushLongListToDisk(int termID) {
	if (terms[termID].numberOfPostings <= 1)
		return;

	offset result[MAX_SEGMENT_SIZE];
	int outPos = 0;

	offset lastOffset = 0;
	int inputChunk = -1;
	byte *inputBuffer = NULL;
	int nextChunk = terms[termID].firstChunk;
	int chunkSize = 0;
	int chunkPos = 0;
	int shift = 0;
	while (true) {
		while (chunkPos < chunkSize - 7) {
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
			*((int32_t*)inputBuffer) = -1;
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
		if (outPos > MAX_SEGMENT_SIZE - 256) {
			longListIndex->addPostings(terms[termID].term, result, MIN_SEGMENT_SIZE);
			memmove(result, &result[MIN_SEGMENT_SIZE], (outPos - MIN_SEGMENT_SIZE) * sizeof(offset));
			outPos -= MIN_SEGMENT_SIZE;
		}
	} // end while (true)

	if (outPos > 0)
		longListIndex->addPostings(terms[termID].term, result, outPos);

	// update term descriptor
	if (terms[termID].memoryConsumed > 256)
		terms[termID].memoryConsumed = 256;
	terms[termID].numberOfPostings = 0;
	terms[termID].firstChunk = terms[termID].currentChunk = -1;
} // end of flushLongListToDisk(int)


void HybridLexicon::recompactPostings() {
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

	// Terminate the container so that we can run this method a second time --
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


void HybridLexicon::createCompactIndex(const char *fileName) {
	log(LOG_ERROR, LOG_ID, "createCompactIndex(char*) called.");
	exit(1);
} // end of createCompactIndex(char*)


void HybridLexicon::mergeWithExisting(IndexIterator **iterators,
		int iteratorCount, char *outputIndex) {
	log(LOG_ERROR, LOG_ID, "mergeWithExisting(IndexIterator**, int, char*) called.");
	exit(1);
} // end of mergeWithExisting(IndexIterator**, int, char*)


void HybridLexicon::mergeWithExisting(IndexIterator **iterators, int iteratorCount,
		char *outputIndex, ExtentList *visible) {
	log(LOG_ERROR, LOG_ID, "mergeWithExisting(...) called.");
	exit(1);
} // end of mergeWithExisting(...)


ExtentList * HybridLexicon::getUpdates(const char *term) {
	LocalLock lock(this);

	ExtentList **result = typed_malloc(ExtentList*, 32);
	int cnt = 0;

	result[cnt++] = longListIndex->getPostings(term);
	if (result[cnt - 1]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)
		delete result[--cnt];

	if (cnt == 0) {
		// we only need to check the non-in-place indices if we haven't found anything
		// in the in-place index (remember that this is *contiguous* index maintenance)
		for (int i = MAX_COMPACTINDEX_COUNT - 1; i >= 0; i--) {
			if (compactIndex[i] != NULL) {
				result[cnt++] = compactIndex[i]->getPostings(term);
				if (result[cnt - 1]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)
					delete result[--cnt];
			}
		}
	} // end if (cnt == 0)

	result[cnt++] = CompressedLexicon::getUpdates(term);
	if (result[cnt - 1]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)
		delete result[--cnt];

	if (cnt == 0) {
		free(result);
		return new ExtentList_Empty();
	}
	else if (cnt == 1) {
		ExtentList *list = result[0];
		free(result);
		return list;
	}
	else
		return new ExtentList_OrderedCombination(result, cnt);
} // end of getUpdates(char*)


void HybridLexicon::getClassName(char *target) {
	strcpy(target, LOG_ID);
} // end of getClassName(char*)



