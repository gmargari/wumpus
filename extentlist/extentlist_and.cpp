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


#include "extentlist.h"
#include "../misc/all.h"


ExtentList_AND::ExtentList_AND(ExtentList *operand1, ExtentList *operand2, int ownership) {
	assert(ownership == TAKE_OWNERSHIP || ownership == DO_NOT_TAKE_OWNERSHIP);
	ownershipOfChildren = ownership;

	elem = (ExtentList**)malloc(2 * sizeof(ExtentList*));
	elem[0] = operand1;
	elem[1] = operand2;
	elemCount = 2;
	checkForMerge();
} // end of ExtentList_AND(ExtentList*, ExtentList*)


ExtentList_AND::ExtentList_AND(ExtentList **elements, int count, int ownership) {
	assert(ownership == TAKE_OWNERSHIP || ownership == DO_NOT_TAKE_OWNERSHIP);
	ownershipOfChildren = ownership;

	elem = elements;
	elemCount = count;
	checkForMerge();
} // end of ExtentList_AND(ExtentList**, int, int)


ExtentList_AND::~ExtentList_AND() {
	if (ownershipOfChildren == TAKE_OWNERSHIP) {
		for (int i = 0; i < elemCount; i++)
			delete elem[i];
	}
	if (elem != NULL) {
		free(elem);
		elem = NULL;
	}
} // end of ~ExtentList_AND()


void ExtentList_AND::checkForMerge() {
	bool allANDs = true;
	for (int i = 0; i < elemCount; i++)
		if (elem[i]->getType() != TYPE_EXTENTLIST_AND)
			allANDs = false;
	if (allANDs) {
		int newElemCount = 0;
		for (int i = 0; i < elemCount; i++) {
			ExtentList_AND *e = (ExtentList_AND*)elem[i];
			newElemCount += e->elemCount;
		}
		ExtentList **newElems = typed_malloc(ExtentList*, newElemCount);
		int pos = 0;
		for (int i = 0; i < elemCount; i++) {
			ExtentList_AND *e = (ExtentList_AND*)elem[i];
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
	} // end if (allANDs)
} // end of checkForMerge()


void ExtentList_AND::detachSubLists() {
	for (int i = 0; i < elemCount; i++)
		elem[i] = NULL;
	elemCount = 0;
} // end of detachSubLists()


bool ExtentList_AND::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	offset lastEnd = position - 1;
	offset s, e;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getFirstStartBiggerEq(position, &s, &e))
			return false;
		if (e > lastEnd)
			lastEnd = e;
	}
	offset firstStart = lastEnd;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getLastEndSmallerEq(lastEnd, &s, &e))
			return false;
		if (s < firstStart)
			firstStart = s;
	}
	*start = firstStart;
	*end = lastEnd;
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_AND::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	offset s, e;
	if (!ExtentList_AND::getLastEndSmallerEq(position - 1, &s, &e))
		s = -1;
	return ExtentList_AND::getFirstStartBiggerEq(s + 1, start, end);
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_AND::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	offset s, e;
	if (!ExtentList_AND::getFirstStartBiggerEq(position + 1, &s, &e))
		e = MAX_OFFSET;
	return ExtentList_AND::getLastEndSmallerEq(e - 1, start, end);
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_AND::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	offset firstStart = position + 1;
	offset s, e;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getLastEndSmallerEq(position, &s, &e))
			return false;
		if (s < firstStart)
			firstStart = s;
	}
	offset lastEnd = firstStart;
	for (int i = 0; i < elemCount; i++) {
		if (!elem[i]->getFirstStartBiggerEq(firstStart, &s, &e))
			return false;
		if (e > lastEnd)
			lastEnd = e;
	}
	*start = firstStart;
	*end = lastEnd;
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


long ExtentList_AND::getMemoryConsumption() {
	long result = 0;
	for (int i = 0; i < elemCount; i++)
		result += elem[i]->getMemoryConsumption();
	return result;
} // end of getMemoryConsumption()


void ExtentList_AND::optimize() {
	for (int i = 0; i < elemCount; i++)
		elem[i]->optimize();
} // end of optimize()


bool ExtentList_AND::isSecure() {
	return false;
} // end of isSecure()


bool ExtentList_AND::isAlmostSecure() {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isAlmostSecure())
			return false;
	return true;
} // end of isAlmostSecure()


ExtentList * ExtentList_AND::makeAlmostSecure(VisibleExtents *restriction) {
	for (int i = 0; i < elemCount; i++)
		if (!elem[i]->isAlmostSecure())
			elem[i] = elem[i]->makeAlmostSecure(restriction);
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_AND::toString() {
	if (elemCount == 1)
		return elem[0]->toString();
	char *result = elem[0]->toString();
	char *newResult = concatenateStrings("(", result);
	free(result);
	result = newResult;
	for (int i = 1; i < elemCount; i++) {
		char *substring = elem[i]->toString();
		char *newResult = concatenateStrings(result, " AND ");
		free(result);
		result = concatenateStrings(newResult, substring);
		free(newResult);
		free(substring);
	}
	newResult = concatenateStrings(result, ")");
	free(result);
	return newResult;
} // end of toString()

