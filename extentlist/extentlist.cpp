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
 * created: 2004-09-23
 * changed: 2008-08-11
 **/


#include <stdio.h>
#include <string.h>
#include "extentlist.h"
#include "../index/postinglist.h"
#include "../filemanager/securitymanager.h"
#include "../misc/all.h"


ExtentList::ExtentList() {
	length = -1;
	totalSize = -1;
	alreadyOptimized = false;
	ownershipOfChildren = TAKE_OWNERSHIP;
} // end of ExtentList()


ExtentList::~ExtentList() {
}


offset ExtentList::getLength() {
	return getCount(0, MAX_OFFSET);
} // end of getLength()


offset ExtentList::getCount(offset start, offset end) {
	static const int CHUNK_SIZE = 1024;
	offset s[CHUNK_SIZE], e[CHUNK_SIZE];
	offset result = 0;
	int n = 0;
	while ((n = getNextN(start, end, CHUNK_SIZE, s, e)) == CHUNK_SIZE) {
		result += CHUNK_SIZE;
		start = s[CHUNK_SIZE - 1] + 1;
	}
	return result + n;
} // end of getCount(offset, offset)


bool ExtentList::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	return false;
}


int ExtentList::getType() {
	return TYPE_EXTENTLIST;
}


void ExtentList::optimize() {
	// nothing to optimize in the abstract ExtentList class
}


void ExtentList::detachSubLists() {
	assert("Not implemented!" == NULL);
}


offset ExtentList::getTotalSize() {
	if (totalSize < 0) {
		offset s, e, position = 0;
		totalSize = length = 0;
		while (getFirstStartBiggerEq(position, &s, &e)) {
			totalSize += (e - s + 1);
			position = s + 1;
			length++;
		}
	}
	return totalSize;
} // end of getTotalSize()


int ExtentList::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	int result = 0;
	while (result < n) {
		if (!getFirstStartBiggerEq(from, &start[result], &end[result]))
			break;
		if (end[result] > to)
			break;
		from = start[result] + 1;
		result++;
	}
	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)


bool ExtentList::getNth(offset n, offset *start, offset *end) {
	*start = -1;
	for (offset i = 0; i <= n; i++)
		if (!getFirstStartBiggerEq(*start + 1, start, end))
			return false;
	return true;
} // end of getNth(offset, offset*, offset*)


bool ExtentList::getInternalValue(char *key, void *target, int targetSize) {
	if (strcasecmp(key, "AVG_SIZE") == 0) {
		if (targetSize < (int)sizeof(double))
			return false;
		if ((length < 0) || (totalSize < 0))
			return false;
		*((double*)target) = (totalSize * 1.0) / length;
		return true;
	}
	return false;
} // end of getInternalValue(char*, void*, int)


long ExtentList::getMemoryConsumption() {
	return 0;
} // end of getMemoryConsumption()


bool ExtentList::isSecure() {
	// by default, stuff is classified as non-secure
	return false;
}


ExtentList * ExtentList::makeSecure(VisibleExtents *restriction) {
	ExtentList *result = this;
	if (!result->isAlmostSecure())
		result = result->makeAlmostSecure(restriction);
	if (!result->isSecure())
		result = restriction->restrictList(result);
	return result;
} // end of makeSecure(VisibleExtents*)


bool ExtentList::isAlmostSecure() {
	// also, we don't even consider it almost secure
	return false;
}


ExtentList * ExtentList::makeAlmostSecure(VisibleExtents *restriction) {
	if (isAlmostSecure())
		return this;
	else
		return restriction->restrictList(this);
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList::toString() {
	return duplicateString("(UNAVAILABLE)");
} // end of toString()


int ExtentList::getInternalPosition() {
	return -1;
}


typedef struct {
	offset *postings;
	offset *nextPosting;
	offset next;
} LHS;

static int lhsComparator(const void *a, const void *b) {
	LHS *x = (LHS*)a;
	LHS *y = (LHS*)b;
	offset difference = x->next - y->next;
	if (difference < 0)
		return -1;
	else if (difference > 0)
		return +1;
	else
		return 0;
} // end of lhsComparator(const void*, const void*)


ExtentList * ExtentList::radixMergeDocumentLevelLists(ExtentList **lists, int listCount) {
	int maxLen = 0;
	for (int i = 0; i < listCount; i++)
		maxLen += lists[i]->getLength();
	offset *result = typed_malloc(offset, maxLen + 2);
	int maxLen2 = 0;
	for (int i = 0; i < listCount; i++) {
		maxLen2 += lists[i]->getNextN(0, MAX_OFFSET, lists[i]->getLength(),
				&result[maxLen2], &result[maxLen2]);
		delete lists[i];
	}
	assert(maxLen == maxLen2);
	free(lists);

	sortOffsetsAscending(result, maxLen);
	result[maxLen] = MAX_OFFSET;
	int cnt = 0;

	for (int i = 0; i < maxLen; i++) {
		if ((result[i] | DOC_LEVEL_MAX_TF) != (result[i + 1] | DOC_LEVEL_MAX_TF))
			result[cnt++] = result[i];
		else {
			offset currentDocument = (result[i] >> DOC_LEVEL_SHIFT);
			offset currentTF = 0;
			while ((result[i] >> DOC_LEVEL_SHIFT) == currentDocument) {
				offset newTF = (result[i] & DOC_LEVEL_MAX_TF);
				if (newTF >= DOC_LEVEL_ENCODING_THRESHOLD)
					newTF = decodeDocLevelTF(newTF);
				currentTF += newTF;
				i++;
			}
			if (currentTF >= DOC_LEVEL_ENCODING_THRESHOLD)
				currentTF = encodeDocLevelTF(currentTF);
			result[cnt++] = (currentDocument << DOC_LEVEL_SHIFT) + currentTF;
			i--;
		}
	}

	result = typed_realloc(offset, result, cnt);
	return new PostingList(result, cnt, false, true);
} // end of radixMergeDocumentLevelLists(ExtentList**, int)


ExtentList * ExtentList::mergeDocumentLevelLists(ExtentList *list1, ExtentList *list2) {
	int len1 = list1->getLength();
	int len2 = list2->getLength();
	offset *array1 = typed_malloc(offset, len1 + 1);
	list1->getNextN(0, MAX_OFFSET, len1, array1, array1);
	delete list1;
	offset *array2 = typed_malloc(offset, len2 + 1);
	list2->getNextN(0, MAX_OFFSET, len2, array2, array2);
	delete list2;

	offset *result = typed_malloc(offset, len1 + len2 + 1);
	int pos1 = 0, pos2 = 0, cnt = 0;
	offset next1 = array1[pos1];
	offset next2 = array2[pos2];

	while (true) {		
		if ((next1 | DOC_LEVEL_MAX_TF) == (next2 | DOC_LEVEL_MAX_TF)) {
			offset tf1 = (next1 & DOC_LEVEL_MAX_TF);
			if (tf1 >= DOC_LEVEL_ENCODING_THRESHOLD)
				tf1 = decodeDocLevelTF(tf1);
			offset tf2 = (next2 & DOC_LEVEL_MAX_TF);
			if (tf2 >= DOC_LEVEL_ENCODING_THRESHOLD)
				tf2 = decodeDocLevelTF(tf2);
			offset tf = tf1 + tf2;
			if (tf >= DOC_LEVEL_ENCODING_THRESHOLD)
				tf = encodeDocLevelTF(tf);
			result[cnt++] = ((next1 >> DOC_LEVEL_SHIFT) << DOC_LEVEL_SHIFT) + tf;
			next1 = array1[++pos1];
			next2 = array2[++pos2];
			if ((pos1 >= len1) || (pos2 >= len2))
				break;
		}
		else if (next1 < next2) {
			result[cnt++] = next1;
			next1 = array1[++pos1];
			if (pos1 >= len1)
				break;
		}
		else {
			result[cnt++] = next2;
			next2 = array2[++pos2];
			if (pos2 >= len2)
				break;
		}
	}
	while (pos1 < len1)
		result[cnt++] = array1[pos1++];
	while (pos2 < len2)
		result[cnt++] = array2[pos2++];

	free(array1);
	free(array2);
	
	result = typed_realloc(offset, result, cnt);
	return new PostingList(result, cnt, false, true);
} // end of mergeDocumentLevelLists(ExtentList*, ExtentList*)



ExtentList * ExtentList::mergeDocumentLevelLists(ExtentList **lists, int listCount) {
	// first, cover all special cases
	if (listCount == 0) {
		free(lists);
		return new ExtentList_Empty();
	}
	if (listCount == 1) {
		ExtentList *result = lists[0];
		free(lists);
		return result;
	}
	if (listCount == 2) {
		ExtentList *list1 = lists[0];
		ExtentList *list2 = lists[1];
		free(lists);
		return mergeDocumentLevelLists(list1, list2);
	}
	if (true)
		return radixMergeDocumentLevelLists(lists, listCount);

	// if none of them are matched, perform multiway merge with a heap		
	LHS *heap = typed_malloc(LHS, listCount);
	int maxLen = 0;
	for (int i = 0; i < listCount; i++) {
		int len = lists[i]->getLength();
		heap[i].postings = typed_malloc(offset, len + 1);
		int n = lists[i]->getNextN(0, MAX_OFFSET, len, heap[i].postings, heap[i].postings);
		assert(n == len);
		heap[i].postings[n] = MAX_OFFSET;
		heap[i].nextPosting = heap[i].postings;
		heap[i].next = heap[i].postings[0];
		delete lists[i];
		maxLen += len;
	}
	free(lists);

	// establish heap property by sorting all lists in increasing order
	// of first element in the respective list
	qsort(heap, listCount, sizeof(LHS), lhsComparator);

	offset *result = typed_malloc(offset, maxLen + 1);
	int cnt = 0;
	offset current = -1;
	offset tf = 0;

	// merge lists until MAX_OFFSET is reached
	while (heap[0].next < MAX_OFFSET) {
		offset next = heap[0].next;

		if ((next | DOC_LEVEL_MAX_TF) != current) {
			if (current >= 0) {
				if (tf >= DOC_LEVEL_ENCODING_THRESHOLD)
					tf = encodeDocLevelTF(tf);
				result[cnt++] = current - DOC_LEVEL_MAX_TF + tf;
			}
			current = (next | DOC_LEVEL_MAX_TF);
			tf = 0;
		}

		offset newTF = (next & DOC_LEVEL_MAX_TF);
		if (newTF >= DOC_LEVEL_ENCODING_THRESHOLD)
			newTF = decodeDocLevelTF(tf);
		tf += newTF;

		// update data structure for top of heap
		heap[0].next = *(++heap[0].nextPosting);

		// perform a reheap operation
		int parent = 0, leftChild = 1, rightChild = 2;
		while (leftChild < listCount) {
			int child = leftChild;
			if (rightChild < listCount)
				if (heap[rightChild].next < heap[leftChild].next)
					child = rightChild;
			if (heap[parent].next <= heap[child].next)
				break;
			LHS temp = heap[parent];
			heap[parent] = heap[child];
			heap[child] = temp;
			parent = child;
			leftChild = parent * 2 + 1;
			rightChild = parent * 2 + 2;
		} // end while (leftChild < listCount)

	} // end while (heap[0].next < MAX_OFFSET)

	// process the last remaining document (if any)
	if (current >= 0) {
		if (tf >= DOC_LEVEL_ENCODING_THRESHOLD)
			tf = encodeDocLevelTF(tf);
		result[cnt++] = current - DOC_LEVEL_MAX_TF + tf;
	}

	for (int i = 0; i < listCount; i++)
		free(heap[i].postings);
	free(heap);
	return new PostingList(result, cnt, false, true);
} // end of mergeDocumentLevelLists(ExtentList**, int)


