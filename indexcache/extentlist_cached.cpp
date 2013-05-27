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
 * Implementation of the ExtentList_Cached class.
 *
 * author: Stefan Buettcher
 * created: 2005-01-06
 * changed: 2006-08-27
 **/


#include <string.h>
#include "extentlist_cached.h"
#include "indexcache.h"
#include "../misc/all.h"


ExtentList_Cached::ExtentList_Cached(IndexCache *cache, int cacheID, offset *start, offset *end, int count) {
	assert(count > 0);
	this->cache = cache;
	this->cacheID = cacheID;
	this->start = start;
	this->end = end;
	this->length = this->count = count;
	currentPosition = 0;
	almostSecure = true;
} // end of ExtentList_Cached(IndexCache*, int, offset*, offset*, int)


ExtentList_Cached::~ExtentList_Cached() {
	if (cache != NULL)
		cache->deregister(cacheID);
	else {
		free(start);
		free(end);
	}
} // end of ~ExtentList_Cached()


static inline void determineInterval(int length, int where, offset what, offset *array, int *l, int *u) {
	// determine interval for binary search
	int lower, upper, delta = 1;
	if (array[where] >= what) {
		upper = where;
		while (upper - delta >= 0) {
			if (array[upper - delta] <= what) {
				*u = where - (delta >> 1);
				*l = where - delta;
				return;
			}
			delta += delta;
		}
		*u = where - (delta >> 1);
		*l = 0;
		return;
	}
	else {
		lower = where;
		while (lower + delta < length) {
			if (array[lower + delta] >= what) {
				*l = where + (delta >> 1);
				*u = where + delta;
				return;
			}
			delta += delta;
		}
		*l = where + (delta >> 1);
		*u = length - 1;
		return;
	}
} // end of determineInterval(...)


bool ExtentList_Cached::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	offset *array = this->start;
	if (position > array[count - 1])
		return false;

	int cp = currentPosition;
	if ((position > array[cp]) && (position <= array[cp + 1])) {
		cp++;
		*start = this->start[cp];
		*end = this->end[cp];
		currentPosition = cp;
		return true;
	}

	int lower, upper;
	determineInterval(count, cp, position, array, &lower, &upper);

	// do binary search between "lower" and "upper"
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (array[middle] < position)
			lower = middle + 1;
		else
			upper = middle;
	}

	currentPosition = lower;
	*start = this->start[lower];
	*end = this->end[lower];
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Cached::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	offset *array = this->end;
	if (position > array[count - 1])
		return false;

	int cp = currentPosition;
	if ((position > array[cp]) && (position <= array[cp + 1])) {
		cp++;
		*start = this->start[cp];
		*end = this->end[cp];
		currentPosition = cp;
		return true;
	}

	int lower, upper;
	determineInterval(count, cp, position, array, &lower, &upper);

	// do binary search between "lower" and "upper"
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (array[middle] < position)
			lower = middle + 1;
		else
			upper = middle;
	}

	currentPosition = lower;
	*start = this->start[lower];
	*end = this->end[lower];
	return true;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Cached::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	offset *array = this->start;
	if (position < array[0])
		return false;

	if (position >= array[count - 1]) {
		currentPosition = count - 1;
		*start = this->start[count - 1];
		*end = this->end[count - 1];
		return true;
	}

	int lower, upper;
	determineInterval(count, currentPosition, position, array, &lower, &upper);
	
	// do binary search between "lower" and "upper"
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (array[middle] > position)
			upper = middle - 1;
		else
			lower = middle;
	}

	currentPosition = lower;
	*start = this->start[lower];
	*end = this->end[lower];
	return true;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Cached::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	offset *array = this->end;
	if (position < array[0])
		return false;

	if (position >= array[count - 1]) {
		currentPosition = count - 1;
		*start = this->start[count - 1];
		*end = this->end[count - 1];
		return true;
	}

	int lower, upper;
	determineInterval(count, currentPosition, position, array, &lower, &upper);
	
	// do binary search between "lower" and "upper"
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (array[middle] > position)
			upper = middle - 1;
		else
			lower = middle;
	}

	currentPosition = lower;
	*start = this->start[lower];
	*end = this->end[lower];
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


int ExtentList_Cached::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	if (!ExtentList_Cached::getFirstStartBiggerEq(from, start, end))
		return 0;
	if (*end > to)
		return 0;
	int result = 1;
	int cp = currentPosition;
	while ((result < n) && (++cp < count)) {
		start[result] = this->start[cp];
		end[result] = this->end[cp];
		if (end[result] > to)
			break;
		result++;
	}
	currentPosition = MIN(cp, count - 1);
	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)


offset ExtentList_Cached::getLength() {
	return count;
}


offset ExtentList_Cached::getCount(offset start, offset end) {
	offset s, e;
	if (!getFirstStartBiggerEq(start, &s, &e))
		return 0;
	int startPos = currentPosition;
	if (!getLastEndSmallerEq(end, &s, &e))
		return 0;
	int endPos = currentPosition;
	if (startPos > endPos)
		return 0;
	else
		return endPos - startPos + 1;
} // end of getCount(offset, offset)


bool ExtentList_Cached::getNth(offset n, offset *start, offset *end) {
	if ((n < 0) || (n >= count))
		return false;
	*start = this->start[n];
	*end = this->end[n];
	return true;
} // end of getNth(offset, offset*, offset*)


bool ExtentList_Cached::isAlmostSecure() {
	return almostSecure;
}


void ExtentList_Cached::setAlmostSecure(bool value) {
	almostSecure = value;
}


char * ExtentList_Cached::toString() {
	return duplicateString("(CACHED)");
}


int ExtentList_Cached::getType() {
	return TYPE_EXTENTLIST_CACHED;
}


int ExtentList_Cached::getInternalPosition() {
	return currentPosition;
}


