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
 * Implementation of the class PostingList. PostingList extends the ExtentList class and is used
 * to represent a posting list that is associated with a certain term.
 *
 * author: Stefan Buettcher
 * created: 2004-09-02
 * changed: 2006-01-18
 **/


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "postinglist.h"
#include "../misc/all.h"


#define READ_ONE_BIT(bitarray, position) \
	((bitarray[position >> 3] & (1 << (position & 7))) != 0 ? 1 : 0)

#define WRITE_ONE_BIT(value, bitarray, position) { \
	if (value != 0) \
		bitarray[position >> 3] |= (1 << (position & 7)); \
	else \
		bitarray[position >> 3] &= (0xFF ^ (1 << (position & 7))); \
}

#define READ_N_BITS(result, n, bitarray, position) { \
	result = 0; \
	for (int chaosCounter = n - 1; chaosCounter >= 0; chaosCounter--) \
		result = (result << 1) + READ_ONE_BIT(bitarray, position + chaosCounter); \
}

#define WRITE_N_BITS(value, n, bitarray, position) { \
	offset tempValue = value; \
	for (int chaosCounter = 0; chaosCounter < n; chaosCounter++) { \
		WRITE_ONE_BIT(tempValue & 1, bitarray, position + chaosCounter) \
		tempValue = tempValue >> 1; \
	} \
}


static int offset_comparator(const void *a, const void *b) {
	offset *x = (offset*)a;
	offset *y = (offset*)b;
	if (*x < *y)
		return -1;
	else if (*x > *y)
		return +1;
	else
		return 0;
} // end of offset_comparator(const void*, const void*)


PostingList::PostingList() {
}


PostingList::PostingList(offset *data, int length, bool copy, bool alreadySorted) {
	assert(length > 0);
	this->length = length;
	if (copy == false)
		this->postings = data;
	else {
		this->postings = (offset*)malloc(length * sizeof(offset));
		memcpy(this->postings, data, length * sizeof(offset));
	}
	if (!alreadySorted)
		sortOffsetsAscending(this->postings, length);
	currentPosition = 0;
} // end of PostingList(offset*, int, bool, bool)


PostingList::~PostingList() {
	if (postings != NULL) {
		free(postings);
		postings = NULL;
	}
} // end of ~PostingList()


offset PostingList::getLength() {
	return length;
} // end of getLength()


offset PostingList::getCount(offset start, offset end) {
	offset s, e;
	if (!PostingList::getFirstStartBiggerEq(start, &s, &e))
		return 0;
	if (e > end)
		return 0;
	int startPosition = currentPosition;
	if (!PostingList::getLastEndSmallerEq(end, &s, &e))
		return 0;
	int endPosition = currentPosition;
	return (endPosition - startPosition + 1);
} // end of getCount(offset, offset)


bool PostingList::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	if (postings[length - 1] < position)
		return false;

	// determine the interval for the binary search
	int lower, upper, delta = 1;
	if (postings[currentPosition] >= position) {
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (postings[upper - delta] <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		upper = upper - (delta >> 1);
		if (lower < 0)
			lower = 0;
	}
	else {
		lower = currentPosition;
		while (lower + delta < length) {
			if (postings[lower + delta] >= position)
				break;
			delta += delta;
		}
		if (delta == 1) {
			*start = *end = postings[++lower];
			currentPosition = lower;
			return true;
		}
		upper = lower + delta;
		lower = lower + (delta >> 1);
		if (upper >= length)
			upper = length - 1;
	}

	// do a binary search
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (postings[middle] < position)
			lower = middle + 1;
		else
			upper = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = postings[currentPosition];
	*end = postings[currentPosition];
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool PostingList::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if (postings[length - 1] < position)
		return false;

	// determine the interval for the binary search
	int lower, upper, delta = 1;
	if (postings[currentPosition] >= position) {
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (postings[upper - delta] <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		upper = upper - (delta >> 1);
		if (lower < 0)
			lower = 0;
	}
	else {
		lower = currentPosition;
		while (lower + delta < length) {
			if (postings[lower + delta] >= position)
				break;
			delta += delta;
		}
		if (delta == 1) {
			*start = *end = postings[++lower];
			currentPosition = lower;
			return true;
		}
		upper = lower + delta;
		lower = lower + (delta >> 1);
		if (upper >= length)
			upper = length - 1;
	}

	// do a binary search
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (postings[middle] < position)
			lower = middle + 1;
		else
			upper = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = postings[currentPosition];
	*end = postings[currentPosition];
	return true;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool PostingList::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if (postings[0] > position)
		return false;

	// determine the interval for the binary search
	int lower, upper, delta = 1;
	if (postings[currentPosition] > position) {
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (postings[upper - delta] <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		upper = upper - (delta >> 1);
		if (lower < 0)
			lower = 0;
	}
	else {
		lower = currentPosition;
		while (lower + delta < length) {
			if (postings[lower + delta] >= position)
				break;
			delta += delta;
		}
		upper = lower + delta;
		lower = lower + (delta >> 1);
		if (upper >= length)
			upper = length - 1;
	}

	// do a binary search
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (postings[middle] > position)
			upper = middle - 1;
		else
			lower = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = postings[currentPosition];
	*end = postings[currentPosition];
	return true;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool PostingList::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	if (postings[0] > position)
		return false;

	// determine the interval for the binary search
	int lower, upper, delta = 1;
	if (postings[currentPosition] > position) {
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (postings[upper - delta] <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		upper = upper - (delta >> 1);
		if (lower < 0)
			lower = 0;
	}
	else {
		lower = currentPosition;
		while (lower + delta < length) {
			if (postings[lower + delta] >= position)
				break;
			delta += delta;
		}
		upper = lower + delta;
		lower = lower + (delta >> 1);
		if (upper >= length)
			upper = length - 1;
	}

	// do a binary search
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (postings[middle] > position)
			upper = middle - 1;
		else
			lower = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = postings[currentPosition];
	*end = postings[currentPosition];
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


int PostingList::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	if (!PostingList::getFirstStartBiggerEq(from, start, end))
		return 0;
	if (*end > to)
		return 0;
	int curPos = currentPosition;
	if (curPos + n < length)
		if (postings[curPos + n] <= to) {
			memcpy(start, &postings[curPos], n * sizeof(offset));
			memcpy(end, &postings[curPos], n * sizeof(offset));
			return n;
		}
	int result = 1;
	while ((result < n) && (++curPos < length)) {
		if (postings[curPos] > to)
			break;
		start[result] = end[result] = postings[curPos];
		result++;
	}
	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)


bool PostingList::getNth(offset n, offset *start, offset *end) {
	if ((n < 0) || (n >= length))
		return false;
	*start = *end = postings[n];
	return true;
} // end of getNth(offset, offset*, offset*)


long PostingList::getMemoryConsumption() {
	return length * sizeof(offset);
}


bool PostingList::isSecure() {
	return false;
}


bool PostingList::isAlmostSecure() {
	return true;
}


ExtentList * PostingList::makeAlmostSecure(VisibleExtents *restriction) {
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * PostingList::toString() {
	return duplicateString("(POSTINGS)");
}


int PostingList::getInternalPosition() {
	return currentPosition;		
}


int PostingList::getType() {
	return TYPE_POSTINGLIST;
}



int findFirstPostingBiggerEq(offset posting, offset *array, int n, int pos) {
	if ((n <= 0) || (array[n - 1] < posting))
		return -1;

	// determine the interval for the binary search
	int lower, upper, delta = 1;
	if (array[pos] >= posting) {
		upper = pos;
		while (upper - delta >= 0) {
			if (array[upper - delta] <= posting)
				break;
			delta += delta;
		}
		lower = upper - delta;
		upper = upper - (delta >> 1);
		if (lower < 0)
			lower = 0;
	}
	else {
		lower = pos;
		while (lower + delta < n) {
			if (array[lower + delta] >= posting)
				break;
			delta += delta;
		}
		if (delta == 1)
			return lower + 1;
		upper = lower + delta;
		lower = lower + (delta >> 1);
		if (upper >= n)
			upper = n - 1;
	}

	// do a binary search in the given range
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (array[middle] < posting)
			lower = middle + 1;
		else
			upper = middle;
	}
	return lower;
} // end of findFirstPostingBiggerEq(offset, offset*, int, int)


int findLastPostingSmallerEq(offset posting, offset *array, int n, int pos) {
	if ((n <= 0) || (array[0] > posting))
		return -1;

	// determine the interval for the binary search
	int lower, upper, delta = 1;
	if (array[pos] > posting) {
		upper = pos;
		while (upper - delta >= 0) {
			if (array[upper - delta] <= posting)
				break;
			delta += delta;
		}
		if (delta == 1)
			return upper - 1;
		lower = upper - delta;
		upper = upper - (delta >> 1);
		if (lower < 0)
			lower = 0;
	}
	else {
		lower = pos;
		while (lower + delta < n) {
			if (array[lower + delta] >= posting)
				break;
			delta += delta;
		}
		upper = lower + delta;
		lower = lower + (delta >> 1);
		if (upper >= n)
			upper = n - 1;
	}

	// do a binary search in the given range
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (array[middle] > posting)
			upper = middle - 1;
		else
			lower = middle;
	}
	return lower;
} // end of findLastPostingSmallerEq(offset, offset*, int, int)




