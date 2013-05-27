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
 * created: 2005-04-24
 * changed: 2009-02-01
 **/


#include <sched.h>
#include <string.h>
#include "index_merger.h"
#include "inplace_index.h"
#include "multiple_index_iterator.h"
#include "ondisk_index.h"
#include "../misc/all.h"


static const char * LOG_ID = "IndexMerger";


void IndexMerger::mergeIndices(Index *index,
		char *outputFile, IndexIterator **iterators, int iteratorCount) {
	MultipleIndexIterator *iterator =
		new MultipleIndexIterator(iterators, iteratorCount);

	CompactIndex *target = CompactIndex::getIndex(index, outputFile, true);
	mergeIndices(index, target, iterator, NULL, false);

	delete iterator;
	delete target;
} // end of mergeIndices(...)


void IndexMerger::mergeIndicesWithGarbageCollection(Index *index,
		char *outputFile, IndexIterator **iterators, int iteratorCount, ExtentList *visible) {
	MultipleIndexIterator *iterator =
		new MultipleIndexIterator(iterators, iteratorCount);

	CompactIndex *target = CompactIndex::getIndex(index, outputFile, true);
	mergeIndices(index, target, iterator, visible, false);

	delete iterator;
	delete target;
} // end of mergeIndicesWithGarbageCollection(...)


int IndexMerger::filterPostingsAgainstIntervals(offset *postings, int listLength,
			offset *intervalStart, offset *intervalEnd, int intervalCount) {
	// since we know that both lists (postings and intervals) are sorted, we
	// can reuse the results from a binary search for one posting for the
	// next posting; this gives us an amortized time complexity of
	// O(m * log(n/m)) instead of O(m * log(n)).

	// do some sanity checking
	if ((intervalCount <= 0) || (listLength <= 0))
		return 0;

	int inPos = 0;
	int outPos = 0;
	int intervalPos = 0;
	offset endOfLastInterval = intervalEnd[intervalCount - 1];

#if 1
	while (true) {
		// filter postings by means of galloping search
		while (postings[inPos] < intervalStart[intervalPos])
			if (++inPos >= listLength)
				return outPos;
		while (postings[inPos] <= intervalEnd[intervalPos]) {
			postings[outPos++] = postings[inPos];
			if (++inPos >= listLength)
				return outPos;
		}

		// terminate if no more visibility intervals possible for current posting
		offset posting = postings[inPos];
		if ((posting > endOfLastInterval) || (intervalPos >= intervalCount - 1))
			break;

		if (intervalEnd[intervalPos + 1] >= posting) {
			intervalPos++;
			continue;
		}

		// perform galloping search for next possible visibility interval
		int delta = 1;
		while (intervalEnd[intervalPos + delta] < posting) {
			delta += delta;
			if (intervalPos + delta >= intervalCount) {
				delta = intervalCount - 1 - intervalPos;
				break;
			}
		}
		int upper = intervalPos + delta;
		while (upper > intervalPos) {
			int middle = (intervalPos + upper) >> 1;
			if (intervalEnd[middle] < posting)
				intervalPos = middle + 1;
			else
				upper = middle;
		}
	} // end while (true)
#endif
#if 0
	// co-sequential filtering algorithm
	for (inPos = 0; inPos < listLength; inPos++) {
		while (intervalEnd[intervalPos] < postings[inPos]) {
			if (++intervalPos >= intervalCount)
				return outPos;
		}
		if (intervalStart[intervalPos] <= postings[inPos])
			postings[outPos++] = postings[inPos];
	}
#endif
#if 0
	// filter postings by means of binary search
	while (postings[inPos] < intervalStart[0])
		if (++inPos >= listLength)
			break;
	while (inPos < listLength) {
		offset posting = postings[inPos++];
		if (posting > endOfLastInterval)
			break;
		int lower = 0, upper = intervalCount - 1;
		while (lower < upper) {
			int middle = (lower + upper + 1) >> 1;
			if (intervalStart[middle] > posting)
				upper = middle - 1;
			else
				lower = middle;
		}
		if (intervalEnd[lower] >= posting)
			postings[outPos++] = posting;
	}
#endif

	return outPos;
} // end of filterPostingsAgainstIntervals(...)


void IndexMerger::mergeIndices(
		Index *index, OnDiskIndex *target, IndexIterator *input, ExtentList *visible, bool lowPriority) {
	// Create arrays of extent start and end positions from the given ExtentList.
	// these two arrays will then be used to filter all postings in the input
	// indices against the list of visible extents (active files).
	offset *start = NULL;
	offset *end = NULL;
	int intervalCount = -1;
	if (visible != NULL) {
		intervalCount = visible->getLength();
		if (intervalCount == 0)
			return;
		start = typed_malloc(offset, intervalCount + 1);
		end = typed_malloc(offset, intervalCount + 1);
		int n, outPos = 0;
		offset position = 0;
		while ((n = visible->getNextN(position, MAX_OFFSET, 256, &start[outPos], &end[outPos])) > 0) {
			outPos += n;
			position = start[outPos - 1] + 1;
		}
		if (outPos != intervalCount) {
			char msg[256];
			snprintf(msg, sizeof(msg), "ExtentList returns crap! type: %d -- %s\n",
					visible->getType(), visible->toString());
			log(LOG_ERROR, LOG_ID, msg);
			snprintf(msg, sizeof(msg), "outPos = %d, intervalCount = %d\n", outPos, intervalCount);
			log(LOG_ERROR, LOG_ID, msg);
		}
		assert(outPos == intervalCount);
	}

	// we use "currentTerm" to keep track of the previously processed term;
	// the assumption is that this term will not change in the current iteration,
	// but instead we will only move on to another index
	char currentTerm[MAX_TOKEN_LENGTH * 2];
	currentTerm[0] = 0;

	const int OUTPUT_BUFFER_SIZE = 3 * MAX_SEGMENT_SIZE;
	offset outputBuffer[OUTPUT_BUFFER_SIZE];
	byte compressedOutputBuffer[6 * 2 * MAX_SEGMENT_SIZE];
	int outputBufferPos = 0;

	offset firstPosting = 0, lastPosting = 0;
	int count = 0, byteLength = 0;

	while (input->hasNext()) {
		// If the caller has asked us to run at low priority, we will honour his
		// will and check for query activity in every iteration of this loop.
		if (lowPriority) {
			assert(index != NULL);
			sched_yield();
			if (index->registeredUserCount > 0) {
				while (index->registeredUserCount > 0)
					usleep(10000);
				char className[256];
				target->getClassName(className);
				if (strcmp(className, "CompactIndex") == 0)
					((CompactIndex*)target)->flushPartialWriteCache();
			}
		} // end if (lowPriority)

		char *nextTerm = input->getNextTerm();
		if (strcmp(nextTerm, currentTerm) != 0) {
			// if the term has changed and we still have postings for the last term in
			// the output buffer, flush them to the target index
			if ((outputBufferPos > 0) && (start != NULL))
				outputBufferPos = filterPostingsAgainstIntervals(
						outputBuffer, outputBufferPos, start, end, intervalCount);
			if (outputBufferPos > 0) {
				target->addPostings(currentTerm, outputBuffer, outputBufferPos);
				outputBufferPos = 0;
			}
			if (byteLength > 0) {
				target->addPostings(currentTerm, compressedOutputBuffer, byteLength,
						count, firstPosting, lastPosting);
				count = byteLength = 0;
			}
			strcpy(currentTerm, nextTerm);
		} // end if (strcmp(nextTerm, currentTerm) != 0)

		PostingListSegmentHeader *header = input->getNextListHeader();
		int hpc = header->postingCount;
		if (hpc > MAX_SEGMENT_SIZE) {
			char errorMessage[64];
			sprintf(errorMessage, "List segment of size %d found.", hpc);
			log(LOG_ERROR, LOG_ID, errorMessage);
			assert(hpc <= MAX_SEGMENT_SIZE);
		}

		if (start == NULL) {
			// merging postings without built-in garbage collection
#if 1
			// if the buffer is empty, we simply copy the compressed list from the
			// input index to the output buffer
			if (byteLength == 0) {
				firstPosting = header->firstElement;
				lastPosting = header->lastElement;
				input->getNextListCompressed(&count, &byteLength, compressedOutputBuffer);
				assert(firstPosting <= lastPosting);
			}
			else {
				int cnt, bLen;
				assert(header->byteLength < 6 * MAX_SEGMENT_SIZE);
				offset newLastPosting = header->lastElement;
				input->getNextListCompressed(&cnt, &bLen, (byte*)outputBuffer);
				// append compressed list in "temp" to compressed list in "compressedOutputBuffer"
				assert(byteLength + bLen < sizeof(compressedOutputBuffer) - 16);
				mergeCompressedLists(compressedOutputBuffer, byteLength, (byte*)outputBuffer,
						bLen, lastPosting, &count, &byteLength, true);
				assert((unsigned int)byteLength < sizeof(compressedOutputBuffer));
				lastPosting = newLastPosting;
				assert(firstPosting <= lastPosting);
				assert(firstPosting <= lastPosting);
			}

			if ((count >= MIN_SEGMENT_SIZE) && (count <= MAX_SEGMENT_SIZE)) {
				target->addPostings(currentTerm, compressedOutputBuffer, byteLength,
						count, firstPosting, lastPosting);
				count = byteLength = 0;
			}
			if (count > MAX_SEGMENT_SIZE) {
				int length;
				decompressList(compressedOutputBuffer, byteLength, &length, outputBuffer);
				assert(length == count);
				int middle = count / 2;
				target->addPostings(currentTerm, &outputBuffer[0], middle);
				target->addPostings(currentTerm, &outputBuffer[middle], count - middle);
				count = byteLength = 0;
			}
#else
			// this is the old implementation, decompressing every list before the
			// merge; it should only be used for comparative purposes
			while (outputBufferPos > TARGET_SEGMENT_SIZE + MIN_SEGMENT_SIZE) {
				target->addPostings(currentTerm, outputBuffer, TARGET_SEGMENT_SIZE);
				outputBufferPos -= TARGET_SEGMENT_SIZE;
				memmove(outputBuffer, &outputBuffer[TARGET_SEGMENT_SIZE],
						outputBufferPos * sizeof(offset));
			}
			assert(outputBufferPos + hpc < OUTPUT_BUFFER_SIZE);
			int length;
			input->getNextListUncompressed(&length, &outputBuffer[outputBufferPos]);
			assert(length == hpc);
			outputBufferPos += length;
#endif
		} // end if (start == NULL)
		else if (start != NULL) {
			// merging postings with integrated garbage collection
			if (outputBufferPos + hpc >= OUTPUT_BUFFER_SIZE) {
				outputBufferPos = filterPostingsAgainstIntervals(outputBuffer,
						outputBufferPos, start, end, intervalCount);
				while (outputBufferPos >= MAX_SEGMENT_SIZE) {
					target->addPostings(currentTerm, outputBuffer, TARGET_SEGMENT_SIZE);
					outputBufferPos -= TARGET_SEGMENT_SIZE;
					memmove(outputBuffer, &outputBuffer[TARGET_SEGMENT_SIZE],
							outputBufferPos * sizeof(offset));
				}
			}
			assert(outputBufferPos + hpc < OUTPUT_BUFFER_SIZE);
			int length;
			input->getNextListUncompressed(&length, &outputBuffer[outputBufferPos]);
			assert(length == hpc);
			outputBufferPos += length;
		} // end else [start != NULL]

	} // end while (iterator->hasNext())

	// if we still have some postings in the output buffer, flush them to the target
	if ((outputBufferPos > 0) && (start != NULL))
		outputBufferPos = filterPostingsAgainstIntervals(outputBuffer,
				outputBufferPos, start, end, intervalCount);
	if (outputBufferPos > 0)
		target->addPostings(currentTerm, outputBuffer, outputBufferPos);
	if (byteLength > 0) {
		target->addPostings(currentTerm, compressedOutputBuffer, byteLength,
				count, firstPosting, lastPosting);
		count = byteLength = 0;
	}

	if (start != NULL)
		free(start);
	if (end != NULL)
		free(end);
} // end of mergeIndices(Index*, CompactIndex*, IndexIterator*, ExtentList*)


void IndexMerger::mergeWithLongTarget(Index *index,
		OnDiskIndex *target, IndexIterator* input, InPlaceIndex *longListTarget,
		int longListThreshold, bool mayAddNewTermsToLong, int newFlag) {
	// we use "currentTerm" to keep track of the previously processed term;
	// the assumption is that this term will not change in the current iteration,
	// but instead we will only move on to another index
	char currentTerm[MAX_TOKEN_LENGTH * 2];
	currentTerm[0] = 0;

	// This is meta-information for not-yet-committed list segments. We need this
	// for longListTarget, since the threshold may be bigger than MAX_SEGMENT_SIZE.
	static const int MAX_PENDING_SEGMENTS = 512;
	offset segmentStart[MAX_PENDING_SEGMENTS], segmentEnd[MAX_PENDING_SEGMENTS];
	int segmentSize[MAX_PENDING_SEGMENTS], segmentLength[MAX_PENDING_SEGMENTS];
	byte *segmentData[MAX_PENDING_SEGMENTS];
	offset *uncompressed = typed_malloc(offset, 2 * MAX_SEGMENT_SIZE);
	int segmentCount = 0;
	offset postingsForCurrentTerm = 0, bytesForCurrentTerm = 0;
	OnDiskIndex *targetForCurrentTerm = target;

	while (input->hasNext()) {
		char *nextTerm = input->getNextTerm();
		PostingListSegmentHeader *header = input->getNextListHeader();
		assert(header->postingCount <= MAX_SEGMENT_SIZE);

		targetForCurrentTerm = target;
		postingsForCurrentTerm = 0;
		bytesForCurrentTerm = 0;
		strcpy(currentTerm, nextTerm);

		while ((nextTerm != NULL) && (segmentCount < MAX_PENDING_SEGMENTS)) {
			if ((strcmp(nextTerm, currentTerm) != 0) || (header->postingCount < MIN_SEGMENT_SIZE))
				break;
			segmentStart[segmentCount] = header->firstElement;
			segmentEnd[segmentCount] = header->lastElement;
			segmentData[segmentCount] =
				input->getNextListCompressed(&segmentLength[segmentCount], &segmentSize[segmentCount], NULL);
			postingsForCurrentTerm += segmentLength[segmentCount];
			bytesForCurrentTerm += segmentSize[segmentCount];
			segmentCount++;
			nextTerm = input->getNextTerm();
			header = input->getNextListHeader();
		}

		if (bytesForCurrentTerm >= longListThreshold) {
			// if the number of postings accumulated for the current term exceeds the
			// user-defined threshold value, check whether we may add the term to
			// the long-list target (i.e., the "appearsInIndex" bitmask only refers to
			// the current target index)
			InPlaceTermDescriptor *descriptor = longListTarget->getDescriptor(currentTerm);
			if (descriptor == NULL) {
				// if the current term does not have an entry in the long list index yet, then
				// we can only move it if all partitions are involved in current merge operation
				if (mayAddNewTermsToLong)
					targetForCurrentTerm = longListTarget;
			}
			else if (descriptor->appearsInIndex == newFlag) {
				// if the current term already has an entry, make sure that it does not appear
				// in any partition that is not involved in the current merge operation
				targetForCurrentTerm = longListTarget;
				descriptor->appearsInIndex = 0;
			}
		} // end if (bytesForCurrentTerm >= longListThreshold)

		// flush any pending segments
		for (int i = 0; i < segmentCount; i++) {
			targetForCurrentTerm->addPostings(currentTerm, segmentData[i],
					segmentSize[i], segmentLength[i], segmentStart[i], segmentEnd[i]);
			free(segmentData[i]);
		}
		segmentCount = 0;

		postingsForCurrentTerm = 0;
		bytesForCurrentTerm = 0;
		while (nextTerm != NULL) {
			assert(header->postingCount <= MAX_SEGMENT_SIZE);
			if (strcmp(nextTerm, currentTerm) != 0)
				break;
			int length;
			bytesForCurrentTerm += header->byteLength;
			input->getNextListUncompressed(&length, &uncompressed[postingsForCurrentTerm]);
			postingsForCurrentTerm += length;
			if (postingsForCurrentTerm > MAX_SEGMENT_SIZE) {
				int todo = postingsForCurrentTerm - MIN_SEGMENT_SIZE;
				targetForCurrentTerm->addPostings(currentTerm, uncompressed, todo);
				postingsForCurrentTerm = MIN_SEGMENT_SIZE;
				memcpy(uncompressed, &uncompressed[todo], MIN_SEGMENT_SIZE * sizeof(offset));
			}
			nextTerm = input->getNextTerm();
			header = input->getNextListHeader();
		}
		if (postingsForCurrentTerm > 0)
			targetForCurrentTerm->addPostings(currentTerm, uncompressed, postingsForCurrentTerm);
	} // end while (iterator->hasNext())

	free(uncompressed);
	longListTarget->finishUpdate();
} // end of mergeWithLongTarget(Index*, OnDiskIndex*, IndexIterator*, OnDiskIndex*, int, bool, int)



