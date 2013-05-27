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
 * created: 2005-01-10
 * changed: 2005-11-28
 **/


#include <string.h>
#include "extentlist.h"
#include "../index/index_types.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"
#include "../misc/logging.h"


static const char *LOG_ID = "ExtentList_OrderedCombination";


void ExtentList_OrderedCombination::initialize(ExtentList **lists, offset *relOffs, int count) {
	this->lists = lists;
	listCount = count;
	currentSubIndex = 0;
	firstStart = typed_malloc(offset, listCount + 1);
	lastStart = typed_malloc(offset, listCount + 1);
	firstEnd = typed_malloc(offset, listCount + 1);
	lastEnd = typed_malloc(offset, listCount + 1);
	if (relOffs != NULL)
		relativeOffsets = relOffs;
	else {
		relativeOffsets = typed_malloc(offset, listCount);
		for (int i = 0; i < listCount; i++)
			relativeOffsets[i] = 0;
	}
	bool ok = true;
	for (int i = 0; i < listCount; i++) {
		if (!lists[i]->getFirstStartBiggerEq(0, &firstStart[i], &firstEnd[i]))
			ok = false;
		if (!lists[i]->getLastEndSmallerEq(MAX_OFFSET, &lastStart[i], &lastEnd[i]))
			ok = false;
		firstStart[i] += relativeOffsets[i];
		firstEnd[i] += relativeOffsets[i];
		lastStart[i] += relativeOffsets[i];
		lastEnd[i] += relativeOffsets[i];
	}
	for (int i = 1; i < listCount; i++) {
		if (firstStart[i] < lastStart[i - 1])
			ok = false;
		if (firstEnd[i] < lastEnd[i - 1])
			ok = false;
	}
	if (!ok) {
		for (int i = 0; i < listCount; i++) {
			char message[256];
			sprintf(message, "Sublist %d: " OFFSET_FORMAT " - " OFFSET_FORMAT,
					i, firstStart[i], lastEnd[i]);
			log(LOG_ERROR, LOG_ID, message);
		}
		assert("Input lists for ExtentList_OrderedCombination are not ordered!" == NULL);
	}
} // end of initialize(ExtentList**, offset*, int)


ExtentList_OrderedCombination::ExtentList_OrderedCombination(ExtentList **lists, int listCount) {
	assert(listCount > 0);
	initialize(lists, NULL, listCount);
} // end of ExtentList_OrderedCombination(ExtentList**, int)


ExtentList_OrderedCombination::ExtentList_OrderedCombination(ExtentList **lists,
			offset *relOffs, int listCount) {
	assert(listCount > 0);
	initialize(lists, relOffs, listCount);
} // end of ExtentList_OrderedCombination(ExtentList**, offset*, int)


ExtentList_OrderedCombination::~ExtentList_OrderedCombination() {
	for (int i = 0; i < listCount; i++)
		delete lists[i];
	if (lists != NULL) {
		free(lists);
		lists = NULL;
	}
	free(firstStart);
	free(lastStart);
	free(firstEnd);
	free(lastEnd);
	free(relativeOffsets);
} // end of ~ExtentList_OrderedCombination()


bool ExtentList_OrderedCombination::getFirstStartBiggerEq(
			offset position, offset *start, offset *end) {
#ifdef OC_DEBUG
	int where = position;
	printf("ExtentList_OrderedCombination::getFirstStartBiggerEq(%d)\n", where);
#endif
	int csi = currentSubIndex;
	if ((position >= firstStart[csi]) && (position <= lastStart[csi])) {
		lists[csi]->getFirstStartBiggerEq(position - relativeOffsets[csi], start, end);
		*start += relativeOffsets[csi];
		*end += relativeOffsets[csi];
		return true;
	}
	for (int i = 0; i < listCount; i++) {
		if (lastStart[i] >= position) {
			bool result =
				lists[i]->getFirstStartBiggerEq(position - relativeOffsets[i], start, end);
			*start += relativeOffsets[i];
			*end += relativeOffsets[i];
			currentSubIndex = i;
			return result;
		}
	}
	return false;
} // end of getFirstStartBiggerEq(...)


bool ExtentList_OrderedCombination::getFirstEndBiggerEq(
			offset position, offset *start, offset *end) {
#ifdef OC_DEBUG
	int where = position;
	printf("ExtentList_OrderedCombination::getFirstEndBiggerEq(%d)\n", where);
#endif
	int csi = currentSubIndex;
	if ((position >= firstEnd[csi]) && (position <= lastEnd[csi])) {
		lists[csi]->getFirstEndBiggerEq(position - relativeOffsets[csi], start, end);
		*start += relativeOffsets[csi];
		*end += relativeOffsets[csi];
		return true;
	}
	for (int i = 0; i < listCount; i++)
		if (lastEnd[i] >= position) {
			bool result =
				lists[i]->getFirstEndBiggerEq(position - relativeOffsets[i], start, end);
			*start += relativeOffsets[i];
			*end += relativeOffsets[i];
			currentSubIndex = i;
			return result;
		}
	return false;
} // end of getFirstEndBiggerEq(...)


bool ExtentList_OrderedCombination::getLastStartSmallerEq(
			offset position, offset *start, offset *end) {
#ifdef OC_DEBUG
	int where = position;
	printf("ExtentList_OrderedCombination::getLastStartSmallerEq(%d)\n", where);
#endif
	int csi = currentSubIndex;
	if ((position >= firstStart[csi]) && (position <= lastStart[csi])) {
		lists[currentSubIndex]->getLastStartSmallerEq(position - relativeOffsets[csi], start, end);
		*start += relativeOffsets[csi];
		*end += relativeOffsets[csi];
		return true;
	}
	for (int i = listCount - 1; i >= 0; i--)
		if (firstStart[i] <= position) {
			bool result =
				lists[i]->getLastStartSmallerEq(position - relativeOffsets[i], start, end);
			*start += relativeOffsets[i];
			*end += relativeOffsets[i];
			currentSubIndex = i;
			return result;
		}
	return false;
} // end of getLastStartSmallerEq(...)


bool ExtentList_OrderedCombination::getLastEndSmallerEq(
			offset position, offset *start, offset *end) {
#ifdef OC_DEBUG
	int where = position;
	printf("ExtentList_OrderedCombination::getLastEndSmallerEq(%d)\n", where);
#endif
	int csi = currentSubIndex;
	if ((position >= firstEnd[csi]) && (position <= lastEnd[csi])) {
		lists[csi]->getLastEndSmallerEq(position - relativeOffsets[csi], start, end);
		*start += relativeOffsets[csi];
		*end += relativeOffsets[csi];
		return true;
	}
	for (int i = listCount - 1; i >= 0; i--)
		if (firstEnd[i] <= position) {
			bool result =
				lists[i]->getLastEndSmallerEq(position - relativeOffsets[i], start, end);
			*start += relativeOffsets[i];
			*end += relativeOffsets[i];
			currentSubIndex = i;
			return result;
		}
	return false;
} // end of getLastEndSmallerEq(...)


offset ExtentList_OrderedCombination::getLength() {
	if (length < 0) {
		length = 0;
		for (int i = 0; i < listCount; i++)
			length += lists[i]->getLength();
	}
	return length;
} // end of getLength()


offset ExtentList_OrderedCombination::getCount(offset start, offset end) {
	offset result = 0;
	for (int i = 0; i < listCount; i++)
		if ((firstEnd[i] <= end) && (lastStart[i] >= start))
			result += lists[i]->getCount(start - relativeOffsets[i], end - relativeOffsets[i]);
	return result;
} // end of getCount(offset, offset)


long ExtentList_OrderedCombination::getMemoryConsumption() {
	long result = 0;
	for (int i = 0; i < listCount; i++)
		result += lists[i]->getMemoryConsumption();
	return result;
} // end of getMemoryConsumption()


void ExtentList_OrderedCombination::optimize() {
	for (int i = 0; i < listCount; i++)
		lists[i]->optimize();
} // end of optimize()


bool ExtentList_OrderedCombination::isSecure() {
	for (int i = 0; i < listCount; i++)
		if (!lists[i]->isSecure())
			return false;
	return true;
} // end of ExtentList_OrderedCombination::isSecure()


bool ExtentList_OrderedCombination::isAlmostSecure() {
	for (int i = 0; i < listCount; i++)
		if (!lists[i]->isAlmostSecure())
			return false;
	return true;
} // end of isAlmostSecure()


ExtentList * ExtentList_OrderedCombination::makeAlmostSecure(VisibleExtents *restriction) {
	for (int i = 0; i < listCount; i++)
		if (!lists[i]->isAlmostSecure())
			lists[i] = lists[i]->makeAlmostSecure(restriction);
	return this;
} // end of makeAlmostSecure(VisibleExtents)


char * ExtentList_OrderedCombination::toString() {
	char *original = lists[0]->toString();
	char *result = (char*)malloc(strlen(original) + 4);
	sprintf(result, "{%s}", original);
	free(original);
	return result;
} // end of toString()


int ExtentList_OrderedCombination::getType() {
	return TYPE_EXTENTLIST_ORDERED;
}



