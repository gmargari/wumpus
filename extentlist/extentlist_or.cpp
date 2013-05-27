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
 * created: 2004-09-24
 * changed: 2008-08-11
 **/


#include <string.h>
#include "extentlist.h"
#include "../config/config.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


ExtentList_OR::ExtentList_OR() {
} // end of ExtentList_OR()


ExtentList_OR::ExtentList_OR(ExtentList *operand1, ExtentList *operand2, int ownership) {
	assert(ownership == TAKE_OWNERSHIP || ownership == DO_NOT_TAKE_OWNERSHIP);
	ownershipOfChildren = ownership;

	elem = (ExtentList**)malloc(2 * sizeof(ExtentList*));
	elem[0] = operand1;
	elem[1] = operand2;
	elemCount = 2;	
	checkForMerge();
} // end of ExtentList_OR(ExtentList*, ExtentList*)


ExtentList_OR::ExtentList_OR(ExtentList **elements, int count, int ownership) {
	assert(ownership == TAKE_OWNERSHIP || ownership == DO_NOT_TAKE_OWNERSHIP);
	ownershipOfChildren = ownership;

	elem = elements;
	elemCount = count;
	checkForMerge();
} // end of ExtentList_OR(ExtentList**, int, int)


ExtentList_OR::~ExtentList_OR() {
	if (ownershipOfChildren == TAKE_OWNERSHIP) {
		for (int i = 0; i < elemCount; i++)
			delete elem[i];
	}
	if (elem != NULL) {
		free(elem);
		elem = NULL;
	}
} // end of ~ExtentList_OR()


/** LHS means ListHeapStruct. It is used to merge the sublists of an ExtentList_OR. **/
typedef struct {
	int who;
	offset nextStart;
	offset nextEnd;
} LHS;


static int lhsComparator(const void *a, const void *b) {
	LHS *x = (LHS*)a;
	LHS *y = (LHS*)b;
	if (x->nextStart != y->nextStart) {
		if (x->nextStart < y->nextStart)
			return -1;
		else
			return +1;
	}
	else if (x->nextEnd != y->nextEnd) {
		if (x->nextEnd > y->nextEnd)
			return -1;
		else
			return +1;
	}
	else
		return 0;
} // end of lhsComparator(const void*, const void*)


static void updateMyHeap(LHS *heap, int heapSize) {
	int parent = 0;
	int leftChild = parent * 2 + 1;
	int rightChild = parent * 2 + 2;
	while (leftChild < heapSize) {
		int child = leftChild;
		if (rightChild < heapSize)
			if (lhsComparator(&heap[rightChild], &heap[leftChild]) < 0)
				child = rightChild;
		if (lhsComparator(&heap[parent], &heap[child]) <= 0)
			break;
		LHS oldParent = heap[parent];
		heap[parent] = heap[child];
		heap[child] = oldParent;
		parent = child;
		leftChild = parent * 2 + 1;
		rightChild = parent * 2 + 2;
	}
} // end of updateMyHeap(LHS*, int)


void ExtentList_OR::checkForMerge() {
	bool allORs = true;
	for (int i = 0; i < elemCount; i++)
		if (elem[i]->getType() != TYPE_EXTENTLIST_OR)
			allORs = false;
	if (allORs) {
		int newElemCount = 0;
		for (int i = 0; i < elemCount; i++) {
			ExtentList_OR *e = (ExtentList_OR*)elem[i];
			newElemCount += e->elemCount;
		}
		ExtentList **newElems = typed_malloc(ExtentList*, newElemCount);
		int pos = 0;
		for (int i = 0; i < elemCount; i++) {
			ExtentList_OR *e = (ExtentList_OR*)elem[i];
			for (int k = 0; k < e->elemCount; k++)
				newElems[pos++] = e->elem[k];
			if (ownershipOfChildren == TAKE_OWNERSHIP) {
				e->detachSubLists();
				delete e;
			}
		}
		assert(pos == newElemCount);
		free(elem);
		elem = newElems;
		elemCount = newElemCount;
	} // end if (allORs)
} // end of checkForMerge()


void ExtentList_OR::optimize() {
	if ((alreadyOptimized) || (ownershipOfChildren != TAKE_OWNERSHIP))
		return;
	alreadyOptimized = true;

	for (int i = 0; i < elemCount; i++)
		elem[i]->optimize();
	if (elemCount <= 1)
		return;

	offset *subListLength = typed_malloc(offset, elemCount);
	offset totalLength = 0;

	// while merging the lists, check whether we have a disjunction of PostingList
	// instances here; if that is the case, we can create a PostingList instance
	// instead of a ExtentList_Cached instance at the end
	bool everythingIsPostingList = true;

	int64_t memoryConsumption = 0;
	for (int i = 0; i < elemCount; i++) {
		int type = elem[i]->getType();
		if ((type != TYPE_POSTINGLIST) && (type != TYPE_SEGMENTEDPOSTINGLIST))
			everythingIsPostingList = false;
		subListLength[i] = elem[i]->getLength();
		totalLength += subListLength[i];
		memoryConsumption += elem[i]->getMemoryConsumption();
	}

	int64_t totalSize =
		totalLength * sizeof(offset) * (everythingIsPostingList ? 1 : 2);
	bool doTheMerge = false;
	if (totalSize <= MERGE_LISTS_THRESHOLD)
		doTheMerge = true;
	if (totalSize <= memoryConsumption * 1.1)
		doTheMerge = true;

	if (doTheMerge) {
		// if the lists are not too long, we can afford to evaluate and merge them
		// immediately, which is much faster than it will be in the future, since we
		// can do a single linear pass now and use a heap to speed up the merging
		mergeChildLists();
		free(subListLength);
		return;
	}

	if (elemCount > 4) {
		// if we cannot merge stuff due to memory limitations, but still have too
		// many sublists, we can at least try to merge all the short ones, which
		// will probably give us a significant performance boost later on
		ExtentList **shortLists = typed_malloc(ExtentList*, elemCount);
		int shortListCount = 0;
		offset combinedLengthOfShortLists = 0;
		offset upperLimitForCombinedLength = MERGE_LISTS_THRESHOLD / (2 * sizeof(offset));
		if (everythingIsPostingList)
			upperLimitForCombinedLength = MERGE_LISTS_THRESHOLD / sizeof(offset);

		for (int i = 0; i < elemCount; i++) {
			if (elem[i] != NULL) {
				offset len = subListLength[i];
				if ((combinedLengthOfShortLists + len < upperLimitForCombinedLength) && (len >= MAX_SEGMENT_SIZE)) {
					shortLists[shortListCount++] = elem[i];
					combinedLengthOfShortLists += subListLength[i];
					// remove old elem from original list, reduce length of list
					subListLength[i] = subListLength[elemCount - 1];
					elem[i--] = elem[--elemCount];
				}
			}
		} // end for (int i = 0; i < elemCount; i++)

		for (int i = 0; i < elemCount; i++) {
			if (elem[i] != NULL) {
				if (subListLength[i] < MAX_SEGMENT_SIZE) {
					shortLists[shortListCount++] = elem[i];
					combinedLengthOfShortLists += subListLength[i];
					// remove old elem from original list, reduce length of list
					subListLength[i] = subListLength[elemCount - 1];
					elem[i--] = elem[--elemCount];
				}
			}
		} // end for (int i = 0; i < elemCount; i++)

		if (shortListCount <= 1) {
			// damn! nothing to merge here...
			if (shortListCount == 1)
				elem[elemCount++] = shortLists[0];
			free(shortLists);
		}
		else {
			// at this point, we have selected all the small lists within this disjunction;
			// we now create a new ExtentList_OR from them and call that guy's optimize()
			// function, which will merge the lists
			ExtentList_OR *newList = new ExtentList_OR(shortLists, shortListCount);
			newList->mergeChildLists();
			assert(newList->elemCount == 1);
			elem[elemCount++] = newList->elem[0];
			newList->elemCount = 0;
			delete newList;
		}

		free(subListLength);
		return;
	} // end if (elemCount > 6)

	free(subListLength);
} // end of optimize()


void ExtentList_OR::mergeChildLists() {
	if (elemCount <= 1)
		return;

	bool everythingIsPostingList = true;
	LHS *listHeap = typed_malloc(LHS, elemCount);
	for (int i = 0; i < elemCount; i++) {
		int type = elem[i]->getType();
		if ((type != TYPE_POSTINGLIST) && (type != TYPE_SEGMENTEDPOSTINGLIST))
			everythingIsPostingList = false;
		listHeap[i].who = i;
		offset s, e;
		if (elem[i]->getFirstStartBiggerEq(0, &s, &e)) {
			listHeap[i].nextStart = s;
			listHeap[i].nextEnd = e;
		}
		else {
			listHeap[i].nextStart = MAX_OFFSET;
			listHeap[i].nextEnd = MAX_OFFSET;
		}
	}
	qsort(listHeap, elemCount, sizeof(LHS), lhsComparator);

	// now we have a heap of LHS instances; use this heap to create a sorted
	// list of index extents
	int allocated = 1024;
	offset *start = typed_malloc(offset, allocated);
	offset *end = typed_malloc(offset, allocated);
	offset s, e;
	int cnt = 0;

	// as long as there are more extents to be retrieved, use the heap to
	// extract the next index extent from the disjunction
	while (listHeap[0].nextStart < MAX_OFFSET) {
		// add new extent to result list
		start[cnt] = listHeap[0].nextStart;
		end[cnt] = listHeap[0].nextEnd;
		if (++cnt >= allocated) {
			allocated = (int)(allocated * 2.5);
			start = typed_realloc(offset, start, allocated);
			end = typed_realloc(offset, end, allocated);
		}

		// update the heap data
		if (elem[listHeap[0].who]->getFirstStartBiggerEq(listHeap[0].nextStart + 1, &s, &e)) {
			listHeap[0].nextStart = s;
			listHeap[0].nextEnd = e;
		}
		else {
			listHeap[0].nextStart = MAX_OFFSET;
			listHeap[0].nextEnd = MAX_OFFSET;
		}
		if (elemCount == 2) {
			// if we have only 2 elements, we don't need the full heap hokuspokus here
			offset difference = listHeap[1].nextStart - listHeap[0].nextStart;
			if ((difference < 0) || ((difference == 0) && (listHeap[1].nextEnd > listHeap[0].nextEnd))) {
				LHS temp = listHeap[0];
				listHeap[0] = listHeap[1];
				listHeap[1] = temp;
			}
		}
		else
			updateMyHeap(listHeap, elemCount);

		while (cnt >= 2) {
			// we have to make sure GCL's "not-nested" condition is never violated;
			// we already know that "start[cnt - 1] >= start[cnt - 2]", so if we find
			// out that "end[cnt - 1] <= end[cnt - 2]", we have to remove (cnt-2) from
			// the list and replace it by (cnt-1)
			if (end[cnt - 1] <= end[cnt - 2]) {
				start[cnt - 2] = start[cnt - 1];
				end[cnt - 2] = end[cnt - 1];
				cnt--;
			}
			else
				break;
		} // end while (cnt >= 2)

	} // end while (listHeap[0].nextStart < MAX_OFFSET)

	free(listHeap);
	for (int i = 0; i < elemCount; i++)
		delete elem[i];
	elemCount = 1;
	if (cnt == 0) {
		free(start);
		free(end);
		elem[0] = new ExtentList_Empty();
	}
	else if (everythingIsPostingList) {
		start = typed_realloc(offset, start, cnt);
		free(end);
		elem[0] = new PostingList(start, cnt, false, true);
	}
	else {
		start = typed_realloc(offset, start, cnt);
		end = typed_realloc(offset, end, cnt);
		elem[0] = new ExtentList_Cached(NULL, -1, start, end, cnt);
	}
} // end of mergeChildLists()


void ExtentList_OR::detachSubLists() {
	for (int i = 0; i < elemCount; i++)
		elem[i] = NULL;
	elemCount = 0;
} // end of detachSubLists()


bool ExtentList_OR::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	offset s, e;
	offset currentStart = -1;
	offset currentEnd = MAX_OFFSET;

	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getFirstStartBiggerEq(position, &s, &e))
			continue;
		if (e <= currentEnd) {
			if (e < currentEnd) {
				currentStart = s;
				currentEnd = e;
			}
			else if (s > currentStart)
				currentStart = s;
		}
	}
	if (currentEnd == MAX_OFFSET)
		return false;
	*start = currentStart;
	*end = currentEnd;
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_OR::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	offset s, e;
	offset currentStart = -1;
	offset currentEnd = MAX_OFFSET;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getFirstEndBiggerEq(position, &s, &e))
			continue;
		if (e <= currentEnd) {
			if (e < currentEnd) {
				currentStart = s;
				currentEnd = e;
			}
			else if (s > currentStart)
				currentStart = s;
		}
	}
	if (currentEnd == MAX_OFFSET)
		return false;
	*start = currentStart;
	*end = currentEnd;
	return true;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_OR::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	offset s, e;
	offset currentStart = -1;
	offset currentEnd = MAX_OFFSET;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getLastStartSmallerEq(position, &s, &e))
			continue;
		if (s >= currentStart) {
			if (s > currentStart) {
				currentStart = s;
				currentEnd = e;
			}
			else if (e < currentEnd)
				currentEnd = e;
		}
	}
	if (currentStart < 0)
		return false;
	*start = currentStart;
	*end = currentEnd;
	return true;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_OR::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	offset s, e;
	offset currentStart = -1;
	offset currentEnd = MAX_OFFSET;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getLastEndSmallerEq(position, &s, &e))
			continue;
		if (s >= currentStart) {
			if (s > currentStart) {
				currentStart = s;
				currentEnd = e;
			}
			else if (e < currentEnd)
				currentEnd = e;
		}
	}
	if (currentStart < 0)
		return false;
	*start = currentStart;
	*end = currentEnd;
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


long ExtentList_OR::getMemoryConsumption() {
	long result = 0;
	for (int i = 0; i < elemCount; i++)
		result += elem[i]->getMemoryConsumption();
	return result;
} // end of getMemoryConsumption()


int ExtentList_OR::getType() {
	return TYPE_EXTENTLIST_OR;
}


bool ExtentList_OR::isSecure() {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isSecure())
			return false;
	return true;
} // end of isSecure()


bool ExtentList_OR::isAlmostSecure() {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isAlmostSecure())
			return false;
	return true;
} // end of isAlmostSecure()


ExtentList * ExtentList_OR::makeAlmostSecure(VisibleExtents *restriction) {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isAlmostSecure())
			elem[i] = elem[i]->makeAlmostSecure(restriction);
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_OR::toString() {
	if (elemCount == 1)
		return elem[0]->toString();
	char *result = elem[0]->toString();
	char *newResult = concatenateStrings("(", result);
	free(result);
	result = newResult;
	for (int i = 1; i < elemCount; i++) {
		result = concatenateStringsAndFree(result, duplicateString(" OR "));
		result = concatenateStringsAndFree(result, elem[i]->toString());
	}
	return concatenateStringsAndFree(result, duplicateString(")"));
} // end of toString()



