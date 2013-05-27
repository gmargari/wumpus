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
 * Implementation of the SegmentedPostingList class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-09
 * changed: 2009-02-01
 **/


#include <string.h>
#include "segmentedpostinglist.h"
#include "compactindex.h"
#include "index_compression.h"
#include "../filemanager/securitymanager.h"
#include "../misc/all.h"


static const char * LOG_ID = "SegmentedPostingList";

#define CONCURRENT_DECOMPRESSION 0


static int onDiskSegmentComparator(const void *a, const void *b) {
	SPL_OnDiskSegment *x = (SPL_OnDiskSegment*)a;
	SPL_OnDiskSegment *y = (SPL_OnDiskSegment*)b;
	offset delta = (x->firstPosting - y->firstPosting);
	if (delta < 0)
		return -1;
	else if (delta > 0)
		return +1;
	else
		return 0;
} // end of onDiskSegmentComparator(const void*, const void*)


static int inMemorySegmentComparator(const void *a, const void *b) {
	SPL_InMemorySegment *x = (SPL_InMemorySegment*)a;
	SPL_InMemorySegment *y = (SPL_InMemorySegment*)b;
	offset delta = (x->firstPosting - y->firstPosting);
	if (delta < 0)
		return -1;
	else if (delta > 0)
		return +1;
	else
		return 0;
} // end of inMemorySegmentComparator(const void*, const void*)


SegmentedPostingList::SegmentedPostingList(SPL_OnDiskSegment *segments, int segmentCount) {
	assert(segmentCount > 0);
	// check whether we have to sort the segment list; if so, do it!
	for (int i = 1; i < segmentCount; i++)
		if (segments[i].firstPosting <= segments[i - 1].lastPosting) {
			sprintf(errorMessage,
					"Unordered segments: " OFFSET_FORMAT " <= " OFFSET_FORMAT "\n",
					segments[i].firstPosting, segments[i - 1].lastPosting);
			log(LOG_ERROR, LOG_ID, errorMessage);
			assert(false);
		}
	this->inMemorySegments = NULL;
	this->onDiskSegments = segments;
	this->segmentCount = segmentCount;
	this->mustFreeCompressedBuffers = true;
	firstPosting = segments[0].firstPosting;
	lastPosting = segments[segmentCount - 1].lastPosting;
	currentFirst = MAX_OFFSET;
	currentLast = 0;
	currentSegmentID = -1;
	totalLength = -1;
	initialized = false;
} // end of SegmentedPostingList(SPL_OnDiskSegment*, int)


SegmentedPostingList::SegmentedPostingList(
		SPL_InMemorySegment *segments, int segmentCount, bool mustFreeCompressedBuffers) {
	assert(segmentCount > 0);
	// check whether we have to sort the segment list; if so, do it!
	for (int i = 1; i < segmentCount; i++)
		if (segments[i].firstPosting <= segments[i - 1].lastPosting) {
			sprintf(errorMessage,
					"Unordered segments: " OFFSET_FORMAT " <= " OFFSET_FORMAT "\n",
					segments[i].firstPosting, segments[i - 1].lastPosting);
			log(LOG_ERROR, LOG_ID, errorMessage);
			assert(false);
		}
	this->inMemorySegments = segments;
	this->onDiskSegments = NULL;
	this->segmentCount = segmentCount;
	this->mustFreeCompressedBuffers = mustFreeCompressedBuffers;
	firstPosting = segments[0].firstPosting;
	lastPosting = segments[segmentCount - 1].lastPosting;
	currentFirst = MAX_OFFSET;
	currentLast = 0;
	currentSegmentID = -1;
	totalLength = -1;
	initialized = false;
} // end of SegmentedPostingList(SPL_InMemorySegment*, int, bool)


SegmentedPostingList::~SegmentedPostingList() {
	if (initialized) {
		// free memory occupied by uncompressed postings (first-level cache)
		for (int i = 0; i < DECOMPRESSED_SEGMENT_COUNT; i++)
			if (decompressedSegments[i].postings != NULL) {
				free(decompressedSegments[i].postings);
				decompressedSegments[i].postings = NULL;
			}
		// free memory occupied by compressed postings (second-level cache); but
		// only do this if in fact they have to be freed; this is the case when we
		// are fed with postings from disk and not from memory
		if (inMemorySegments == NULL) {
			for (int i = 0; i < IN_MEMORY_SEGMENT_COUNT; i++)
				if (compressedSegments[i].postings != NULL) {
					free(compressedSegments[i].postings);
					compressedSegments[i].postings = NULL;
				}
		}
	} // end if (initialized)

	if (onDiskSegments != NULL) {
		for (int i = 0; i < segmentCount; i++)
			delete onDiskSegments[i].file;
		free(onDiskSegments);
		onDiskSegments = NULL;
	}
	if (inMemorySegments != NULL) {
		if (mustFreeCompressedBuffers)
			for (int i = 0; i < segmentCount; i++)
				free(inMemorySegments[i].postings);
		free(inMemorySegments);
		inMemorySegments = NULL;
	}
} // end of ~SegmentedPostingList()


static const int MAX_CONCURRENT_SEGMENTS = 256;
typedef struct {
	byte *compressedData[MAX_CONCURRENT_SEGMENTS];
	int byteLength[MAX_CONCURRENT_SEGMENTS];
	int segmentID[MAX_CONCURRENT_SEGMENTS];
	bool terminate;
} SPL_ConcurrentDecompressionStruct;


static void *asynchronousListDecompressor(void *data) {
	SPL_ConcurrentDecompressionStruct *cds =
		(SPL_ConcurrentDecompressionStruct*)data;
	for (int i = 0; i < MAX_CONCURRENT_SEGMENTS; i++) {
		do {
			sched_yield();
		} while (cds->compressedData[i] == NULL);
		if (cds->terminate)
			break;
		int listLength;
		offset *uncompressed =
			decompressList(cds->compressedData[i], cds->byteLength[i], &listLength, NULL);
		free(cds->compressedData[i]);
		cds->compressedData[i] =
			compressNone(uncompressed, listLength, &cds->byteLength[i]);
		free(uncompressed);
	}
	return NULL;
} // end of asynchronousListDecompressor(void*)


void SegmentedPostingList::initialize() {
	if (initialized)
		return;
	totalLength = 0;

	// fill second-level cache with data from in-memory segments
	if (inMemorySegments != NULL) {
		for (int i = 0; i < segmentCount; i++) {
			inMemorySegments[i].segmentID = i;
			totalLength += inMemorySegments[i].count;
		}
		for (int i = 0; i < IN_MEMORY_SEGMENT_COUNT; i++) {
			if (i < segmentCount) {
				compressedSegments[i] = inMemorySegments[i];
				compressedSegments[i].timeStamp = currentTimeStamp++;
			}
			else {
				compressedSegments[i].postings = NULL;
				compressedSegments[i].timeStamp = -1;
			}
		}
	} // end if (inMemorySegments != NULL)

	// fill second-level cache with data from on-disk segments
	if (onDiskSegments != NULL) {
#if CONCURRENT_DECOMPRESSION
		SPL_ConcurrentDecompressionStruct cds;
		memset(cds.compressedData, 0, sizeof(cds.compressedData));
		cds.terminate = false;
		pthread_t decompressor;
		pthread_create(&decompressor, NULL, asynchronousListDecompressor, &cds);
#endif
		for (int i = 0; i < IN_MEMORY_SEGMENT_COUNT; i++) {
			if (i < segmentCount) {
				compressedSegments[i].postings = (byte*)malloc(onDiskSegments[i].byteLength);
				onDiskSegments[i].file->seekAndRead(
						0, onDiskSegments[i].byteLength, compressedSegments[i].postings);
				compressedSegments[i].count = onDiskSegments[i].count;
				compressedSegments[i].firstPosting = onDiskSegments[i].firstPosting;
				compressedSegments[i].lastPosting = onDiskSegments[i].lastPosting;
				compressedSegments[i].segmentID = i;
				compressedSegments[i].byteLength = onDiskSegments[i].byteLength;
				compressedSegments[i].timeStamp = currentTimeStamp++;
#if CONCURRENT_DECOMPRESSION
				if (i < MAX_CONCURRENT_SEGMENTS) {
					cds.byteLength[i] = compressedSegments[i].byteLength;
					cds.segmentID[i] = i;
					cds.compressedData[i] = compressedSegments[i].postings;
				}
#endif
			}
			else {
				compressedSegments[i].postings = NULL;
				compressedSegments[i].timeStamp = -1;
			}
		}
#if CONCURRENT_DECOMPRESSION
		cds.terminate = true;
		pthread_join(decompressor, NULL);
		for (int i = 0; i < MAX_CONCURRENT_SEGMENTS; i++)
			if (cds.compressedData[i] != 0) {
				int id = cds.segmentID[i];
				compressedSegments[id].postings = cds.compressedData[i];
				compressedSegments[id].byteLength = cds.byteLength[i];
			}
#endif
		for (int i = 0; i < segmentCount; i++)
			totalLength += onDiskSegments[i].count;
	} // end if (onDiskSegments != NULL)

	// fill first-level cache with data from second-level cache
	for (int i = 0; i < DECOMPRESSED_SEGMENT_COUNT; i++) {
		SPL_DecompressedSegment *seg = &decompressedSegments[i];
		seg->postings = NULL;
		seg->timeStamp = -1;
		if (i < segmentCount) {
			int length;
			SPL_DecompressedSegment *seg = &decompressedSegments[i];
			seg->postings = decompressList(compressedSegments[i].postings,
					compressedSegments[i].byteLength, &length, NULL);
			assert(length == compressedSegments[i].count);
			seg->count = length;
			seg->segmentID = i;
			seg->timeStamp = currentTimeStamp++;
		}
	} // end for (int i = 0; i < DECOMPRESSED_SEGMENT_COUNT; i++)

	currentSegment = decompressedSegments[0].postings;
	if (currentSegment == NULL) {
		currentFirst = MAX_OFFSET;
		currentLast = 0;
	}
	else {
		currentFirst = decompressedSegments[0].postings[0];
		currentLast = decompressedSegments[0].postings[decompressedSegments[0].count - 1];
	}
	currentSegmentID = 0;
	currentSegmentLength = decompressedSegments[0].count;
	currentPosition = 0;
	assert(totalLength > 0);
	
	initialized = true;
} // end of initialize()


offset SegmentedPostingList::getLength() {
	if (totalLength < 0) {
		totalLength = 0;
		for (int i = 0; i < segmentCount; i++)
			totalLength +=
				(onDiskSegments ? onDiskSegments[i].count : inMemorySegments[i].count);
	}
	return totalLength;
} // end of getLength()


/** Binary search method used by getFirstStartBiggerEq and getFirstEndBiggerEq. **/
static inline void getFirstBiggerEq(offset *postings, int count, int *position,
		offset where, offset *start, offset *end) {
	if (where <= postings[0]) {
		*start = *end = postings[0];
		*position = 0;
	}
	else {
		// determine the interval for binary search
		int lower, upper, delta;
		lower = upper = *position;
		delta = 1;
		if (postings[lower] >= where) {
			while (upper - delta >= 0) {
				if (postings[upper - delta] <= where)
					break;
				delta += delta;
			}
			lower = upper - delta;
			upper = upper - (delta >> 1);
			if (lower < 0)
				lower = 0;
		}
		else {
			while (lower + delta < count) {
				if (postings[lower + delta] >= where)
					break;
				delta += delta;
			}
			if (delta == 1) {
				*start = *end = postings[++lower];
				*position = lower;
				return;
			}
			upper = lower + delta;
			lower = lower + (delta >> 1);
			if (upper >= count)
				upper = count - 1;
		}

		// perform a binary search between "lower" and "upper"
		while (upper > lower) {
			int middle = (upper + lower) >> 1;
			if (postings[middle] < where)
				lower = middle + 1;
			else
				upper = middle;
		}

		// update internal variables and return result
		*position = lower;
		*start = *end = postings[lower];
	}
} // end of getFirstBiggerEq(...)


static inline void getLastSmallerEq(offset *postings, int count, int *position,
		offset where, offset *start, offset *end) {
	if (where >= postings[count - 1]) {
		*start = *end = postings[count - 1];
		*position = count - 1;
	}
	else {
		// determine interval for binary search
		int lower, upper, delta;
		lower = upper = *position;
		delta = 1;
		if (postings[lower] > where) {
			while (upper - delta >= 0) {
				if (postings[upper - delta] <= where)
					break;
				delta += delta;
			}
			lower = upper - delta;
			upper = upper - (delta >> 1);
			if (lower < 0)
				lower = 0;
		}
		else {
			while (lower + delta < count) {
				if (postings[lower + delta] >= where)
					break;
				delta += delta;
			}
			upper = lower + delta;
			lower = lower + (delta >> 1);
			if (upper >= count)
				upper = count - 1;
		}

		// perform a binary search between "lower" and "upper"
		while (upper > lower) {
			int middle = (upper + lower + 1) >> 1;
			if (postings[middle] > where)
				upper = middle - 1;
			else
				lower = middle;
		}

		// update internal variables and return result
		*position = lower;
		*start = *end = postings[lower];
	}
} // end of getLastSmallerEq(...)


offset SegmentedPostingList::getCount(offset start, offset end) {
	// an attempt to give an efficient implementation of the "getCount" function:
	// use the "segmentLength" values for all segments that lie completely within
	// the given interval; for the (at most) two boundary cases, we have to look
	// at the actual postings in the segments
	offset s, e;
	if (!initialized)
		initialize();

	// speed things up if we know for sure that end interval affects only
	// a single list segment
	if ((start >= currentFirst) && (end <= currentLast)) {
		getFirstBiggerEq(currentSegment, currentSegmentLength, &currentPosition,
				start, &s, &e);
		int first = currentPosition;
		getLastSmallerEq(currentSegment, currentSegmentLength, &currentPosition,
				end, &s, &e);
		int last = currentPosition;
		return last - first + 1;
	}

	int startPosition, startSegment;
	int endPosition, endSegment;

	// find position of first occurrence in interval
	if (!SegmentedPostingList::getFirstStartBiggerEq(start, &s, &e))
		return 0;
	if (e > end)
		return 0;
	startPosition = currentPosition;
	startSegment = currentSegmentID;

	// find position of last occurrence in interval
	if (!SegmentedPostingList::getLastEndSmallerEq(end, &s, &e))
		return 0;
	if (s < start)
		return 0;
	endPosition = currentPosition;
	endSegment = currentSegmentID;

	if (startSegment == endSegment)
		return (endPosition - startPosition + 1);

	offset result = (onDiskSegments ?
			onDiskSegments[startSegment].count : inMemorySegments[startSegment].count) - startPosition;
	result += endPosition + 1;
	// count postings in segments between first and last occurrence
	for (int i = startSegment + 1; i < endSegment; i++)
		result += (onDiskSegments ? onDiskSegments[i].count : inMemorySegments[i].count);

	return result;
} // end of getCount(offset, offset)


bool SegmentedPostingList::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	if ((position < currentFirst) || (position > currentLast)) {
		loadFirstSegmentBiggerEq(position);
		if (currentLast < position)
			return false;
	}
	getFirstBiggerEq(currentSegment, currentSegmentLength, &currentPosition,
		position, start, end);
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool SegmentedPostingList::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if ((position < currentFirst) || (position > currentLast)) {
		loadFirstSegmentBiggerEq(position);
		if (currentLast < position)
			return false;
	}
	getFirstBiggerEq(currentSegment, currentSegmentLength, &currentPosition,
			position, start, end);
	return true;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool SegmentedPostingList::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if ((position < currentFirst) || (position > currentLast)) {
		loadLastSegmentSmallerEq(position);
		if (currentFirst > position)
			return false;
	}
	getLastSmallerEq(currentSegment, currentSegmentLength, &currentPosition,
			position, start, end);
	return true;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool SegmentedPostingList::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	if ((position < currentFirst) || (position > currentLast)) {
		loadLastSegmentSmallerEq(position);
		if (currentFirst > position)
			return false;
	}
	getLastSmallerEq(currentSegment, currentSegmentLength, &currentPosition,
			position, start, end);
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


void SegmentedPostingList::loadFirstSegmentBiggerEq(offset position) {
	if (!initialized)
		initialize();
	if ((currentFirst <= position) && (currentLast >= position))
		return;
	if (position <= firstPosting) {
		loadSegment(0);
		return;
	}
	if (currentLast < position) {
		for (int i = currentSegmentID + 1; i < segmentCount; i++) {
			offset lastPosting =
				(onDiskSegments ? onDiskSegments[i].lastPosting : inMemorySegments[i].lastPosting);
			if (lastPosting >= position) {
				loadSegment(i);
				return;
			}
		}
	}
	else {
		for (int i = currentSegmentID - 1; i >= 0; i--) {
			offset firstPosting =
				(onDiskSegments ? onDiskSegments[i].firstPosting : inMemorySegments[i].firstPosting);
			if (firstPosting <= position) {
				offset lastPosting =
					(onDiskSegments ? onDiskSegments[i].lastPosting : inMemorySegments[i].lastPosting);
				loadSegment(lastPosting >= position ? i : i + 1);
				return;
			}
		}
	}
} // end of loadFirstSegmentBiggerEq(offset)


void SegmentedPostingList::loadLastSegmentSmallerEq(offset position) {
	if (!initialized)
		initialize();
	if ((currentFirst <= position) && (currentLast >= position))
		return;
	if (position >= lastPosting) {
		loadSegment(segmentCount - 1);
		return;
	}
	if (currentFirst > position) {
		for (int i = currentSegmentID - 1; i >= 0; i--) {
			offset firstPosting =
				(onDiskSegments ? onDiskSegments[i].firstPosting : inMemorySegments[i].firstPosting);
			if (firstPosting <= position) {
				loadSegment(i);
				return;
			}
		}
	}
	else {
		for (int i = currentSegmentID + 1; i < segmentCount; i++) {
			offset lastPosting =
				(onDiskSegments ? onDiskSegments[i].lastPosting : inMemorySegments[i].lastPosting);
			if (lastPosting >= position) {
				offset firstPosting =
					(onDiskSegments ? onDiskSegments[i].firstPosting : inMemorySegments[i].firstPosting);
				loadSegment(firstPosting <= position ? i : i - 1);
				return;
			}
		}
	}
} // end of loadLastSegmentSmallerEq(offset)


int SegmentedPostingList::loadSegmentIntoL2(int id) {	
	int toEvict = 0;
	for (int i = 0; i < IN_MEMORY_SEGMENT_COUNT; i++) {
		if (compressedSegments[i].segmentID == id) {
			toEvict = i;
			break;
		}
		if (compressedSegments[i].timeStamp < compressedSegments[toEvict].timeStamp)
			toEvict = i;
	}
	if (compressedSegments[toEvict].segmentID == id)
		compressedSegments[toEvict].timeStamp = currentTimeStamp++;
	else if (onDiskSegments != NULL) {
		if (compressedSegments[toEvict].postings != NULL)
			free(compressedSegments[toEvict].postings);
		compressedSegments[toEvict].postings = (byte*)malloc(onDiskSegments[id].byteLength);
		onDiskSegments[id].file->seekAndRead(
				0, onDiskSegments[id].byteLength, compressedSegments[toEvict].postings);
		compressedSegments[toEvict].count = onDiskSegments[id].count;
		compressedSegments[toEvict].firstPosting = onDiskSegments[id].firstPosting;
		compressedSegments[toEvict].lastPosting = onDiskSegments[id].lastPosting;
		compressedSegments[toEvict].byteLength = onDiskSegments[id].byteLength;
		compressedSegments[toEvict].segmentID = id;
		compressedSegments[toEvict].timeStamp = currentTimeStamp++;
	}
	else {
		compressedSegments[toEvict] = inMemorySegments[id];
		compressedSegments[toEvict].timeStamp = currentTimeStamp++;
	}
	return toEvict;
} // end of SegmentedPostingList::loadSegmentIntoL2(int)


bool SegmentedPostingList::isSegmentInL2(int id) {
	for (int i = 0; i < IN_MEMORY_SEGMENT_COUNT; i++)
		if (compressedSegments[i].segmentID == id)
			return true;
	return false;
}


void SegmentedPostingList::loadSegment(int id) {
	// load the segment with the given ID into the L2 cache
	int whereInL2 = loadSegmentIntoL2(id);

	// try to find out whether we have a sequential access pattern, in which
	// case we preload the following READ_AHEAD_SEGMENT_COUNT segments (if
	// they are not in the cache yet)
	if ((currentSegmentID == id - 1) && (!isSegmentInL2(id + 1))) {
		for (int i = 1; (i <= READ_AHEAD_SEGMENT_COUNT) && (id + i < segmentCount); i++)
			loadSegmentIntoL2(id + i);
	}

	// load the segment from second-level cache into first-level cache
	int toEvict = 0;
	for (int i = 0; i < DECOMPRESSED_SEGMENT_COUNT; i++) {
		if (decompressedSegments[i].segmentID == id) {
			toEvict = i;
			break;
		}
		if (decompressedSegments[i].timeStamp < decompressedSegments[toEvict].timeStamp)
			toEvict = i;
	}
	if (decompressedSegments[toEvict].segmentID == id)
		decompressedSegments[toEvict].timeStamp = currentTimeStamp++;
	else {
		if (decompressedSegments[toEvict].postings != NULL)
			free(decompressedSegments[toEvict].postings);
		int length;
		decompressedSegments[toEvict].postings =
			decompressList(compressedSegments[whereInL2].postings,
					compressedSegments[whereInL2].byteLength, &length, NULL);
		assert(length == compressedSegments[whereInL2].count);
		if (decompressedSegments[toEvict].postings[0] != compressedSegments[whereInL2].firstPosting) {
			printf(OFFSET_FORMAT " != " OFFSET_FORMAT "\n",
					decompressedSegments[toEvict].postings[0], compressedSegments[whereInL2].firstPosting);
		}
		assert(decompressedSegments[toEvict].postings[0] == compressedSegments[whereInL2].firstPosting);
		decompressedSegments[toEvict].count = compressedSegments[whereInL2].count;
		decompressedSegments[toEvict].segmentID = compressedSegments[whereInL2].segmentID;
		decompressedSegments[toEvict].timeStamp = currentTimeStamp++;
	}

	// update internal variables for currently selected segment
	currentSegment = decompressedSegments[toEvict].postings;
	currentSegmentID = decompressedSegments[toEvict].segmentID;
	currentSegmentLength = decompressedSegments[toEvict].count;
	currentFirst = currentSegment[0];
	currentLast = currentSegment[currentSegmentLength - 1];
	currentPosition = 0;
} // end of loadSegment(int)


int SegmentedPostingList::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	int outPos = 0;
	while (outPos < n) {
		if (!SegmentedPostingList::getFirstStartBiggerEq(from, &start[outPos], &end[outPos]))
			break;
		if (end[outPos] > to)
			break;

		int pos = currentPosition;
		assert(pos >= 0);

		// if we can fill the remainder of the buffer with data from the current
		// segment, do so and return
		if ((currentLast <= to) && (n - outPos < currentSegmentLength - pos)) {
			int size = (n - outPos) * sizeof(offset);
			memcpy(&start[outPos], &currentSegment[pos], size);
			memcpy(&end[outPos], &currentSegment[pos], size);
			return n;
		}

		// otherwise, take as many postings from the current segment as possible
		// and then advance to the next segment
		outPos++; pos++;
		offset *pocs = currentSegment;
		while ((outPos < n) && (pos < currentSegmentLength)) {
			start[outPos] = end[outPos] = pocs[pos++];
			if (end[outPos] > to)
				return outPos;
			outPos++;
		}

		from = start[outPos - 1] + 1;
	}

	return outPos;
} // end of getNextN(offset, offset, int, offset*, offset*)


offset * SegmentedPostingList::toArray() {
	if (!initialized)
		initialize();
	offset *result = typed_malloc(offset, getLength());
	int outPos = 0;
	for (int i = 0; i < segmentCount; i++) {
		int whereInL2 = loadSegmentIntoL2(i);
		int segmentLength;
		decompressList(compressedSegments[whereInL2].postings,
				compressedSegments[whereInL2].byteLength, &segmentLength, &result[outPos]);
		outPos += segmentLength;
	}
	assert(outPos == totalLength);
	return result;
} // end of toArray()


bool SegmentedPostingList::getNth(offset n, offset *start, offset *end) {
	if (!initialized)
		initialize();
	if ((n < 0) || (n >= getLength()))
		return false;
	for (int i = 0; i < segmentCount; i++) {
		int currentSegmentLength = (onDiskSegments ? onDiskSegments[i].count : inMemorySegments[i].count);
		if (currentSegmentLength > n) {
			loadSegment(i);
			*start = *end = currentSegment[n];
			return true;
		}
		else
			n -= currentSegmentLength;
	}
	assert("We should never get here!" == NULL);
	return false;
} // end of getNth(offset, offset*, offset*)


long SegmentedPostingList::getMemoryConsumption() {
	long result = 0;
	if (onDiskSegments != NULL)
		result += segmentCount * sizeof(SPL_OnDiskSegment);
	if (inMemorySegments != NULL) {
		result += segmentCount * sizeof(SPL_OnDiskSegment);
		for (int i = 0; i < segmentCount; i++)
			result += inMemorySegments[i].byteLength;
	}
	if (initialized) {
		for (int i = 0; i < DECOMPRESSED_SEGMENT_COUNT; i++)
			if (decompressedSegments[i].postings != NULL)
				result += decompressedSegments[i].count * sizeof(offset);
		if (inMemorySegments == NULL) {
			for (int i = 0; i < IN_MEMORY_SEGMENT_COUNT; i++)
				if (compressedSegments[i].postings != NULL)
					result += compressedSegments[i].byteLength;
		}
	}
	result += sizeof(SegmentedPostingList);
	return result;
} // end of getMemoryConsumption()


bool SegmentedPostingList::isSecure() {
	return false;
}


bool SegmentedPostingList::isAlmostSecure() {
	return true;
}


ExtentList * SegmentedPostingList::makeAlmostSecure(VisibleExtents *restriction) {
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * SegmentedPostingList::toString() {
	return duplicateString("(SEGPOSTINGS)");
}


int SegmentedPostingList::getType() {
	return TYPE_SEGMENTEDPOSTINGLIST;
}


