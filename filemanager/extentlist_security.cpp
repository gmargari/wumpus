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
 * created: 2005-02-20
 * changed: 2005-03-14
 **/


#include "extentlist_security.h"
#include "../misc/all.h"


ExtentList_Security::ExtentList_Security(VisibleExtents *visible) {
	this->visible = visible;
	extents = visible->extents;
	count = visible->count;
	currentPosition = 0;
	visible->usageCounter++;
} // end of ExtentList_Security(FileManager *fm, VisibleExtent*, int)


ExtentList_Security::~ExtentList_Security() {
	visible->usageCounter--;
} // end of ~ExtentList_Security()


#define START_OFFSET(i) (extents[i].startOffset)

#define END_OFFSET(i) (extents[i].startOffset + extents[i].tokenCount - 1)


bool ExtentList_Security::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	// cover trivial cases
	if (count == 0)
		return false;
	if (position <= START_OFFSET(0)) {
		currentPosition = 0;
		*start = START_OFFSET(0);
		*end = END_OFFSET(0);
		return true;
	}
	if (position > START_OFFSET(count - 1))
		return false;
	
	// determine the interval for binary search
	int lower, upper;
	if (START_OFFSET(currentPosition) > position) {
		int delta = 1;
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (START_OFFSET(upper - delta) <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		if (lower < 0)
			lower = 0;
		upper = upper - (delta >> 1);
	}
	else {
		int delta = 1;
		lower = currentPosition;
		while (lower + delta < count) {
			if (START_OFFSET(lower + delta) >= position)
				break;
			delta += delta;
		}
		upper = lower + delta;
		if (upper >= count)
			upper = count - 1;
		lower = lower + (delta >> 1);
	}

	// do the actual binary search to determine the exact array position
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (START_OFFSET(middle) < position)
			lower = middle + 1;
		else
			upper = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = START_OFFSET(lower);
	*end = END_OFFSET(lower);
	return true;
} // end of getFirstStartBiggerEq(...)


bool ExtentList_Security::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	// cover trivial cases
	if (count == 0)
		return false;
	if (position <= END_OFFSET(0)) {
		currentPosition = 0;
		*start = START_OFFSET(0);
		*end = END_OFFSET(0);
		return true;
	}
	if (position > END_OFFSET(count - 1))
		return false;
	
	// determine the interval for binary search
	int lower, upper;
	if (END_OFFSET(currentPosition) > position) {
		int delta = 1;
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (END_OFFSET(upper - delta) <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		if (lower < 0)
			lower = 0;
		upper = upper - (delta >> 1);
	}
	else {
		int delta = 1;
		lower = currentPosition;
		while (lower + delta < count) {
			if (END_OFFSET(lower + delta) >= position)
				break;
			delta += delta;
		}
		upper = lower + delta;
		if (upper >= count)
			upper = count - 1;
		lower = lower + (delta >> 1);
	}

	// do the actual binary search to determine the exact array position
	while (upper > lower) {
		int middle = (upper + lower) >> 1;
		if (END_OFFSET(middle) < position)
			lower = middle + 1;
		else
			upper = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = START_OFFSET(lower);
	*end = END_OFFSET(lower);
	return true;
} // end of getFirstEndBiggerEq(...)


bool ExtentList_Security::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	// cover trivial cases
	if (count == 0)
		return false;
	if (position >= START_OFFSET(count - 1)) {
		currentPosition = count - 1;
		*start = START_OFFSET(count - 1);
		*end = END_OFFSET(count - 1);
		return true;
	}
	if (position < START_OFFSET(0))
		return false;
	
	// determine the interval for binary search
	int lower, upper;
	if (START_OFFSET(currentPosition) > position) {
		int delta = 1;
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (START_OFFSET(upper - delta) <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		if (lower < 0)
			lower = 0;
		upper = upper - (delta >> 1);
	}
	else {
		int delta = 1;
		lower = currentPosition;
		while (lower + delta < count) {
			if (START_OFFSET(lower + delta) >= position)
				break;
			delta += delta;
		}
		upper = lower + delta;
		if (upper >= count)
			upper = count - 1;
		lower = lower + (delta >> 1);
	}

	// do the actual binary search to determine the exact array position
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (START_OFFSET(middle) > position)
			upper = middle - 1;
		else
			lower = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = START_OFFSET(lower);
	*end = END_OFFSET(lower);
	return true;
} // end of getLastStartSmallerEq(...)


bool ExtentList_Security::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	// cover trivial cases
	if (count == 0)
		return false;
	if (position >= END_OFFSET(count - 1)) {
		currentPosition = count - 1;
		*start = START_OFFSET(count - 1);
		*end = END_OFFSET(count - 1);
		return true;
	}
	if (position < END_OFFSET(0))
		return false;
	
	// determine the interval for binary search
	int lower, upper;
	if (END_OFFSET(currentPosition) > position) {
		int delta = 1;
		upper = currentPosition;
		while (upper - delta >= 0) {
			if (END_OFFSET(upper - delta) <= position)
				break;
			delta += delta;
		}
		lower = upper - delta;
		if (lower < 0)
			lower = 0;
		upper = upper - (delta >> 1);
	}
	else {
		int delta = 1;
		lower = currentPosition;
		while (lower + delta < count) {
			if (END_OFFSET(lower + delta) >= position)
				break;
			delta += delta;
		}
		upper = lower + delta;
		if (upper >= count)
			upper = count - 1;
		lower = lower + (delta >> 1);
	}

	// do the actual binary search to determine the exact array position
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (END_OFFSET(middle) > position)
			upper = middle - 1;
		else
			lower = middle;
	}

	// update internal variables and return result
	currentPosition = lower;
	*start = START_OFFSET(lower);
	*end = END_OFFSET(lower);
	return true;
} // end of getLastEndSmallerEq(...)


int ExtentList_Security::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	if (!getFirstStartBiggerEq(from, start, end))
		return 0;
	if (*end > to)
		return 0;
	int result = 1;
	while ((result < n) && (++currentPosition < count)) {
		start[result] = START_OFFSET(currentPosition);
		end[result] = END_OFFSET(currentPosition);
		result++;
	}
	if (currentPosition >= count)
		currentPosition = count - 1;
	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)


offset ExtentList_Security::getLength() {
	return count;
} // end of getLength()


offset ExtentList_Security::getCount(offset start, offset end) {
	offset s, e;
	int startIndex, endIndex;
	if (!ExtentList_Security::getFirstStartBiggerEq(start, &s, &e))
		return 0;
	startIndex = currentPosition;
	if (!ExtentList_Security::getLastEndSmallerEq(end, &s, &e))
		return 0;
	endIndex = currentPosition;
	return endIndex - startIndex + 1;
} // end of getCount(offset, offset)


offset ExtentList_Security::getTotalSize() {
	offset result = 0;
	for (int i = 0; i < count; i++)
		result += extents[i].tokenCount;
	return result;
} // end of getTotalSize()


bool ExtentList_Security::isSecure() {
	return true;
}


bool ExtentList_Security::isAlmostSecure() {
	return true;
}


ExtentList * ExtentList_Security::makeAlmostSecure(VisibleExtents *restriction) {
	return this;
}


char * ExtentList_Security::toString() {
	return duplicateString("(SECURITY)");
} // end of toString()


int ExtentList_Security::getType() {
	return TYPE_EXTENTLIST_SECURITY;
}


