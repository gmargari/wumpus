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
 * Implementation of the ExtentList_Sequence class. ExtentList_Sequence is
 * used to realize phrases by combining the information found in two or more
 * posting lists.
 *
 * author: Stefan Buettcher
 * created: 2004-09-23
 * changed: 2006-12-15
 **/


#include <stdlib.h>
#include <string.h>
#include "extentlist.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


ExtentList_Sequence::ExtentList_Sequence(ExtentList **elements, int count) {
	elem = elements;
	elemCount = count;
	curStart = typed_malloc(offset, elemCount + 1);
	curEnd = typed_malloc(offset, elemCount + 1);
	tokenLength = 0;
	for (int i = 0; i < count; i++)
		if (elem[i]->getFirstStartBiggerEq(0, &curStart[i], &curEnd[i]))
			tokenLength += curEnd[i] - curStart[i] + 1;
} // end of ExtentList_Sequence(ExtentList **elements, int count)


ExtentList_Sequence::~ExtentList_Sequence() {
	for (int i = 0; i < elemCount; i++)
		if (elem[i] != NULL)
			delete elem[i];
	if (elem != NULL) {
		free(elem);
		elem = NULL;
	}
	free(curStart);
	free(curEnd);
} // end of ~ExtentList_Sequence()


offset ExtentList_Sequence::getLength() {
	if (length < 0) {
		if (elemCount == 1)
			length = elem[0]->getLength();
		else {
			offset s, e;
			offset position = 0;
			length = 0;
			while (ExtentList_Sequence::getFirstStartBiggerEq(position, &s, &e)) {
				length++;
				position = s + 1;
			}
		}
	}
	return length;
} // end of getLength()


offset ExtentList_Sequence::getCount(offset start, offset end) {
	if (elemCount == 1)
		return elem[0]->getCount(start, end);
	else {
		offset s, e;
		offset result = 0;
		offset position = start;
		while (ExtentList_Sequence::getFirstStartBiggerEq(position, &s, &e)) {
			if (e > end)
				return result;
			result++;
			position = s + 1;
		}
		return result;
	}
} // end of getCount(offset, offset)


bool ExtentList_Sequence::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	bool found = false;
	while (!found) {
		found = true;
		for (int i = 0; i < elemCount; i++) {
			if (!elem[i]->getFirstStartBiggerEq(position, &curStart[i], &curEnd[i]))
				return false;
			if ((i > 0) && (curStart[i] != position))
				found = false;
			position = curEnd[i] + 1;
		}
		if (!found)
			position = curEnd[elemCount - 1] - tokenLength + 1;
	}
	*start = curStart[0];
	*end = curEnd[elemCount - 1];
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Sequence::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if (ExtentList_Sequence::getLastEndSmallerEq(position - 1, start, end))
		position = *start + 1;
	else
		position = 0;
	return ExtentList_Sequence::getFirstStartBiggerEq(position, start, end);
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Sequence::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if (ExtentList_Sequence::getFirstStartBiggerEq(position + 1, start, end))
		position = *end - 1;
	else
		position = MAX_OFFSET;
	return ExtentList_Sequence::getLastEndSmallerEq(position, start, end);
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Sequence::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	bool found = false;
	while (!found) {
		found = true;
		for (int i = elemCount - 1; i >= 0; i--) {
			if (!elem[i]->getLastEndSmallerEq(position, &curStart[i], &curEnd[i]))
				return false;
			if ((i < elemCount - 1) && (curEnd[i] != position))
				found = false;
			position = curStart[i] - 1;
		}
		if (!found)
			position = curStart[0] + tokenLength - 1;
	}
	*start = curStart[0];
	*end = curEnd[elemCount - 1];
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


void ExtentList_Sequence::optimize() {
	for (int i = 0; i < elemCount; i++)
		elem[i]->optimize();
	offset min = MAX_OFFSET;
	long memoryConsumption = 0;
	for (int i = 0; i < elemCount; i++) {
		offset length = elem[i]->getLength();
		if (length < min)
			min = length;
		memoryConsumption += elem[i]->getMemoryConsumption();
	}
	if (min == 0) {
		for (int i = 0; i < elemCount; i++)
			delete elem[i];
		elemCount = 1;
		elem[0] = new ExtentList_Empty();
	}
	else if ((min * sizeof(offset) <= COMPUTE_IMMEDIATE_THRESHOLD) ||
	         (min * sizeof(offset) <= memoryConsumption * 1.1)) {
		offset *start = typed_malloc(offset, min);
		offset *end = typed_malloc(offset, min);
		offset s = -1, e;
		int cnt = 0;
		while (ExtentList_Sequence::getFirstStartBiggerEq(s + 1, &s, &e)) {
			start[cnt] = s;
			end[cnt] = e;
			cnt++;
		}
		for (int i = 0; i < elemCount; i++)
			delete elem[i];
		elemCount = 1;
		if (cnt == 0) {
			free(start);
			free(end);
			elem[0] = new ExtentList_Empty();
		}
		else {
			typed_realloc(offset, start, cnt);
			typed_realloc(offset, end, cnt);
			elem[0] = new ExtentList_Cached(NULL, -1, start, end, cnt);
		}
	}
} // end of optimize()


long ExtentList_Sequence::getMemoryConsumption() {
	long result = 0;
	for (int i = 0; i < elemCount; i++)
		result += elem[i]->getMemoryConsumption();
	return result;
} // end of getMemoryConsumption()


bool ExtentList_Sequence::isSecure() {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isSecure())
			return false;
	return true;
} // end of isSecure()


bool ExtentList_Sequence::isAlmostSecure() {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isAlmostSecure())
			return false;
	return true;
} // end of isAlmostSecure()


ExtentList * ExtentList_Sequence::makeAlmostSecure(VisibleExtents *restriction) {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isAlmostSecure())
			elem[i] = elem[i]->makeAlmostSecure(restriction);
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_Sequence::toString() {
	char *result = duplicateString("");
	for (int i = 0; i < elemCount; i++) {
		result = concatenateStringsAndFree(result, elem[i]->toString());
		if (i < elemCount - 1)
			result = concatenateStringsAndFree(result, duplicateString("."));
	}
	return result;
} // end of toString()


int ExtentList_Sequence::getType() {
	return TYPE_EXTENTLIST_SEQUENCE;
}




