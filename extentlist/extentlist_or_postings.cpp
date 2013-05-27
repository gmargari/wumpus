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
 * created: 2006-12-24
 * changed: 2007-09-07
 **/


#include <string.h>
#include "extentlist.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


ExtentList_OR_Postings::ExtentList_OR_Postings(ExtentList *operand1, ExtentList *operand2) {
	elem = typed_malloc(ExtentList*, elemCount = 2);
	elem[0] = operand1;
	elem[1] = operand2;
	previewArray = NULL;
	heap = NULL;
} // end of ExtentList_OR_Postings(ExtentList*, ExtentList*)


ExtentList_OR_Postings::ExtentList_OR_Postings(ExtentList **elements, int count) {
	assert(count > 1);
	elem = elements;
	elemCount = count;
	previewArray = NULL;
	heap = NULL;
} // end of ExtentList_OR_Postings(ExtentList**, int)


ExtentList_OR_Postings::~ExtentList_OR_Postings() {
	if (previewArray != NULL) {
		for (int i = 0; i < elemCount; i++)
			free(previewArray[i].preview);
		FREE_AND_SET_TO_NULL(previewArray);
		FREE_AND_SET_TO_NULL(heap);
	}
} // end of ~ExtentList_OR_Postings()


void ExtentList_OR_Postings::optimize() {
	if ((elemCount <= 1) || (alreadyOptimized))
		return;
	alreadyOptimized = true;

	LongLongPair *listLengths = typed_malloc(LongLongPair, elemCount);
	for (int i = 0; i < elemCount; i++) {
		listLengths[i].first = i;
		listLengths[i].second = elem[i]->getLength();
	}
	sortArrayOfLongLongPairsBySecond(listLengths, elemCount);

	// determine the set of child-lists that may participate in the merge
	// operation
	int limit = MERGE_LISTS_THRESHOLD / sizeof(offset);
	int accumulated = 0, selected = 0;
	for (int i = 0; i < elemCount; i++) {
		long long thisLength = listLengths[i].second;
		if ((thisLength <= MAX_SEGMENT_SIZE) || (accumulated + thisLength <= limit)) {
			accumulated += thisLength;
			selected++;
		}
	}
	if (selected <= 1) {
		free(listLengths);
		return;
	}

	// copy postings from selected short lists into common buffer and sort
	offset *postings = typed_malloc(offset, accumulated);
	int outPos = 0;
	for (int i = 0; i < elemCount; i++) {
		long long who = listLengths[i].first;
		long long thisLength = listLengths[i].second;
		if ((thisLength <= MAX_SEGMENT_SIZE) || (outPos + thisLength <= limit)) {
			outPos += elem[who]->getNextN(
					0, MAX_OFFSET, thisLength, &postings[outPos], &postings[outPos]);
			delete elem[who];
			elem[who] = NULL;
		}
	}
	assert(outPos == accumulated);
	free(listLengths);
	accumulated = sortOffsetsAscendingAndRemoveDuplicates(postings, accumulated);

	// adjust "elem" array and create PostingList instance from merged list
	outPos = 0;
	for (int i = 0; i < elemCount; i++)
		if (elem[i] != NULL)
			elem[outPos++] = elem[i];
	elem[outPos++] = new PostingList(postings, accumulated, false, true);
	elemCount = outPos;
} // end of optimize()


bool ExtentList_OR_Postings::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	offset best = MAX_OFFSET;
	offset temp;
	for (int i = 0; i < elemCount; i++)
		if (elem[i]->getFirstStartBiggerEq(position, &temp, &temp))
			if (temp < best)
				best = temp;
	if (best < MAX_OFFSET) {
		*start = *end = best;
		return true;
	}
	else
		return false;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_OR_Postings::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	offset best = MAX_OFFSET;
	offset temp;
	for (int i = 0; i < elemCount; i++)
		if (elem[i]->getFirstEndBiggerEq(position, &temp, &temp))
			if (temp < best)
				best = temp;
	if (best < MAX_OFFSET) {
		*start = *end = best;
		return true;
	}
	else
		return false;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_OR_Postings::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	offset best = -1;
	offset temp;
	for (int i = 0; i < elemCount; i++)
		if (elem[i]->getLastStartSmallerEq(position, &temp, &temp))
			if (temp > best)
				best = temp;
	if (best >= 0) {
		*start = *end = best;
		return true;
	}
	else
		return false;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_OR_Postings::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	offset best = -1;
	offset temp;
	for (int i = 0; i < elemCount; i++)
		if (elem[i]->getLastEndSmallerEq(position, &temp, &temp))
			if (temp > best)
				best = temp;
	if (best >= 0) {
		*start = *end = best;
		return true;
	}
	else
		return false;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


static int ExtentList_OR_Postings__mergeComparator(const void *a, const void *b) {
	ExtentList_OR_Postings__PreviewStruct *x = (ExtentList_OR_Postings__PreviewStruct*)a;
	ExtentList_OR_Postings__PreviewStruct *y = (ExtentList_OR_Postings__PreviewStruct*)b;
	if (x->currentValue < y->currentValue)
		return -1;
	else if (x->currentValue > y->currentValue)
		return +1;
	else
		return 0;
} // end of ExtentList_OR_Postings__mergeComparator(void*, void*)


offset ExtentList_OR_Postings::getCount(offset start, offset end) {
	if (elemCount <= 1)
		return elem[0]->getCount(start, end);

	if (previewArray == NULL) {
		previewArray = typed_malloc(ExtentList_OR_Postings__PreviewStruct, elemCount);
		for (int i = 0; i < elemCount; i++)
			previewArray[i].preview = typed_malloc(offset, ExtentList_OR_Postings__PREVIEW_SIZE);
		heap = typed_malloc(ExtentList_OR_Postings__PreviewStruct*, elemCount);
	}
	for (int i = 0; i < elemCount; i++) {
		previewArray[i].current = previewArray[i].preview;
		previewArray[i].end = &previewArray[i].preview[ExtentList_OR_Postings__PREVIEW_SIZE];
		int status = elem[i]->getNextN(start, end,
				ExtentList_OR_Postings__PREVIEW_SIZE, previewArray[i].preview, previewArray[i].preview);
		if (status < ExtentList_OR_Postings__PREVIEW_SIZE)
			previewArray[i].preview[status] = MAX_OFFSET;
		previewArray[i].currentValue = previewArray[i].preview[0];
		previewArray[i].dataSource = elem[i];
	}

	offset result = 0, last = -1;

	if (elemCount <= 4) {
		// if the number of sub-lists is small, we do not perform a full-blown
		// n-way heap-based merge operation, but simply carry out an iterated
		// linear probing task
		while (true) {
			int best = 0;
			offset bestValue = *previewArray[0].current;
			for (int i = 1; i < elemCount; i++)
				if (*previewArray[i].current < bestValue) {
					best = i;
					bestValue = *previewArray[i].current;
				}
			if ((bestValue > end) || (bestValue >= MAX_OFFSET))
				break;
			if (bestValue > last) {
				last = bestValue;
				result++;
			}
			if (++previewArray[best].current == previewArray[best].end) {
				int status = previewArray[best].dataSource->getNextN(
					bestValue + 1, end, ExtentList_OR_Postings__PREVIEW_SIZE, previewArray[best].preview, previewArray[best].preview);
				previewArray[best].current = previewArray[best].preview;
				if (status < ExtentList_OR_Postings__PREVIEW_SIZE)
					previewArray[best].preview[status] = MAX_OFFSET;
			}
		}
		return result;
	}
	else {
		// perform an n-way merge operation to count the total number of occurrences
		// within the given index extent
		qsort(previewArray, elemCount,
				sizeof(ExtentList_OR_Postings__PreviewStruct), ExtentList_OR_Postings__mergeComparator);
		for (int i = 0; i < elemCount; i++)
			heap[i] = &previewArray[i];

		ExtentList_OR_Postings__PreviewStruct *heapTop = heap[0];
		offset heapTopValue = heapTop->currentValue;
		while ((heapTopValue <= end) && (heapTopValue < MAX_OFFSET)) {
			if (heapTopValue > last) {
				last = heapTopValue;
				result++;
			}

			if (++(heapTop->current) == heapTop->end) {
				// reload data from current top of heap
				int status = heapTop->dataSource->getNextN(
					heapTopValue + 1, end, ExtentList_OR_Postings__PREVIEW_SIZE, heapTop->preview, heapTop->preview);
				heapTop->current = heapTop->preview;
				if (status < ExtentList_OR_Postings__PREVIEW_SIZE)
					heapTop->preview[status] = MAX_OFFSET;
			}
			heapTopValue = heapTop->currentValue = *(heapTop->current);

			// move heap top down in tree in order to restore heap property
			int node = 0, child = 1;
			while (child < elemCount) {
				if (child < elemCount - 1)
					if (heap[child]->currentValue > heap[child + 1]->currentValue)
						child++;
				if (heapTopValue <= heap[child]->currentValue)
					break;
				heap[node] = heap[child];
				node = child;
				child = node + node + 1;
			}
			heap[node] = heapTop;

			heapTop = heap[0];
			heapTopValue = heapTop->currentValue;
		} // end while ((heapTopValue <= end) && (heapTopValue < MAX_OFFSET))

		return result;
	}
} // end of getCount(offset, offset)


int ExtentList_OR_Postings::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	static const int CHUNK_SIZE = 1024;
	static const int BUFFER_SIZE = CHUNK_SIZE * 3;
	offset postings[BUFFER_SIZE];

	int result = 0;
	while (n > 0) {
		int chunkSize = MIN(n, CHUNK_SIZE);

		// iterate over all child-lists and extract the first n postings
		offset currentTo = to;
		int outPos = 0;
		for (int i = 0; i < elemCount; i++) {
			if (outPos + chunkSize > BUFFER_SIZE) {
				assert(outPos > chunkSize);
				outPos = sortOffsetsAscendingAndRemoveDuplicates(postings, outPos);
				outPos = chunkSize;
				currentTo = postings[chunkSize - 1];
			}
			int result = elem[i]->getNextN(from, currentTo, chunkSize, &postings[outPos], &postings[outPos]);
			if (result == chunkSize)
				currentTo = postings[outPos + chunkSize - 1];
			outPos += result;
		}

		// remove duplicates and copy into output buffer
		outPos = sortOffsetsAscendingAndRemoveDuplicates(postings, outPos);
		outPos = MIN(outPos, chunkSize);
		memcpy(start, postings, outPos * sizeof(offset));
		memcpy(end, postings, outPos * sizeof(offset));

		// update counters and pointers so we can go into next iteration
		result += outPos;
		from = start[outPos - 1] + 1;
		start = &start[outPos];
		end = &end[outPos];

		n -= chunkSize;
	} // end while (n > 0)

	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)



