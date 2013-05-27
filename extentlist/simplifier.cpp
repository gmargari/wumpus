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
 * Implementation of the Simplifier class. See simplifier.h for details.
 *
 * author: Stefan Buettcher
 * created: 2005-08-23
 * changed: 2007-09-07
 **/


#include <string.h>
#include "simplifier.h"
#include "../index/postinglist.h"
#include "../index/segmentedpostinglist.h"
#include "../misc/all.h"


ExtentList * Simplifier::simplifyList(ExtentList *list) {
	if (list == NULL)
		return NULL;
	switch (list->getType()) {
		case ExtentList::TYPE_EXTENTLIST_AND:
			return simplify_AND((ExtentList_AND*)list);
		case ExtentList::TYPE_EXTENTLIST_CONTAINMENT:
			return simplify_Containment((ExtentList_Containment*)list);
		case ExtentList::TYPE_EXTENTLIST_OR:
			return simplify_OR((ExtentList_OR*)list);
		case ExtentList::TYPE_EXTENTLIST_ORDERED:
			return simplify_OrderedCombination((ExtentList_OrderedCombination*)list);
		case ExtentList::TYPE_EXTENTLIST_SEQUENCE:
			return simplify_Sequence((ExtentList_Sequence*)list);
		default:
			return list;
	}
} // end of simplifyList(ExtentList*)


ExtentList * Simplifier::combineSegmentedPostingLists(ExtentList **lists, int count) {
	bool onDiskSeen = false, inMemorySeen = false;
	for (int i = 0; i < count; i++) {
		if (lists[i]->getType() != ExtentList::TYPE_SEGMENTEDPOSTINGLIST)
			return NULL;
		SegmentedPostingList *spl = (SegmentedPostingList*)lists[i];
		if (spl->inMemorySegments != NULL)
			inMemorySeen = true;
		else
			onDiskSeen = true;
	}
	if ((onDiskSeen) && (inMemorySeen))
		return NULL;

	if (onDiskSeen) {
		SPL_OnDiskSegment *segments = typed_malloc(SPL_OnDiskSegment, 1);
		int segmentCount = 0;

		for (int i = 0; i < count; i++) {
			// For each list in the input array, merge the segments found in that list
			// with the existing segment list. Since we expect the number of input lists
			// to be small, this leads to an acceptable overall performance.
			SegmentedPostingList *spl = (SegmentedPostingList*)lists[i];

			SPL_OnDiskSegment *newSegments =
				typed_malloc(SPL_OnDiskSegment, segmentCount + spl->segmentCount + 1);
			int segmentPos = 0, newSegmentPos = 0, newSegmentCount = 0;
			while ((segmentPos < segmentCount) && (newSegmentPos < spl->segmentCount)) {
				if (segments[segmentPos].firstPosting < spl->onDiskSegments[newSegmentPos].firstPosting)
					newSegments[newSegmentCount++] = segments[segmentPos++];
				else
					newSegments[newSegmentCount++] = spl->onDiskSegments[newSegmentPos++];
			}
			if (segmentPos < segmentCount) {
				memcpy(&newSegments[newSegmentCount], &segments[segmentPos],
						(segmentCount - segmentPos) * sizeof(SPL_OnDiskSegment));
				newSegmentCount += (segmentCount - segmentPos);
			}
			if (newSegmentPos < spl->segmentCount) {
				memcpy(&newSegments[newSegmentCount], &spl->onDiskSegments[newSegmentPos],
						(spl->segmentCount - newSegmentPos) * sizeof(SPL_OnDiskSegment));
				newSegmentCount += (spl->segmentCount - newSegmentPos);
			}

			free(segments);
			segments = newSegments;
			segmentCount = newSegmentCount;
			spl->segmentCount = 0;
		}
		return new SegmentedPostingList(segments, segmentCount);
	} // end if (onDiskSeen)

	if (inMemorySeen) {
		SPL_InMemorySegment *segments = typed_malloc(SPL_InMemorySegment, 1);
		int segmentCount = 0;

		for (int i = 0; i < count; i++) {
			// For each list in the input array, merge the segments found in that list
			// with the existing segment list. Since we expect the number of input lists
			// to be small, this leads to an acceptable overall performance.
			SegmentedPostingList *spl = (SegmentedPostingList*)lists[i];
			assert(spl->mustFreeCompressedBuffers);

			SPL_InMemorySegment *newSegments =
				typed_malloc(SPL_InMemorySegment, segmentCount + spl->segmentCount + 1);
			int segmentPos = 0, newSegmentPos = 0, newSegmentCount = 0;
			while ((segmentPos < segmentCount) && (newSegmentPos < spl->segmentCount)) {
				if (segments[segmentPos].firstPosting < spl->inMemorySegments[newSegmentPos].firstPosting)
					newSegments[newSegmentCount++] = segments[segmentPos++];
				else
					newSegments[newSegmentCount++] = spl->inMemorySegments[newSegmentPos++];
			}
			if (segmentPos < segmentCount) {
				memcpy(&newSegments[newSegmentCount], &segments[segmentPos],
						(segmentCount - segmentPos) * sizeof(SPL_InMemorySegment));
				newSegmentCount += (segmentCount - segmentPos);
			}
			if (newSegmentPos < spl->segmentCount) {
				memcpy(&newSegments[newSegmentCount], &spl->inMemorySegments[newSegmentPos],
						(spl->segmentCount - newSegmentPos) * sizeof(SPL_InMemorySegment));
				newSegmentCount += (spl->segmentCount - newSegmentPos);
			}

			free(segments);
			segments = newSegments;
			segmentCount = newSegmentCount;
			spl->segmentCount = 0;
		}
		return new SegmentedPostingList(segments, segmentCount, true);
	} // end if (inMemorySeen)

	assert(false);
	return NULL;
} // end of combineSegmentedPostingLists(SegmentedPostingList**, int)


ExtentList * Simplifier::simplify_OrderedCombination(ExtentList_OrderedCombination *list) {
	bool shifted = false;
	bool allPL = true;
	bool someSPL = true;

	// check types of all sublists and hope they are the same
	for (int i = 0; i < list->listCount; i++) {
		if (list->relativeOffsets[i] != 0)
			shifted = true;
		if (list->lists[i]->getType() != ExtentList::TYPE_POSTINGLIST)
			allPL = false;
		if (i <= 1)
			someSPL = someSPL && (list->lists[i]->getType() == ExtentList::TYPE_SEGMENTEDPOSTINGLIST);
	} // end for (int i = 0; i < list->listCount; i++)

	if (list->listCount == 0) {
		delete list;
		return new ExtentList_Empty();
	}

	if (allPL) {
		offset postingCount = 0;
		for (int i = 0; i < list->listCount; i++)
			postingCount += list->lists[i]->getLength();
		offset *postings = typed_malloc(offset, postingCount + 1);
		offset outPos = 0;
		for (int i = 0; i < list->listCount; i++) {
			PostingList *pl = (PostingList*)list->lists[i];
			offset relOff = list->relativeOffsets[i];
			if (relOff != 0)
				for (int k = 0; k < pl->length; k++)
					postings[outPos + k] = pl->postings[k] + relOff;
			else
				memcpy(&postings[outPos], pl->postings, pl->length * sizeof(offset));
			outPos += pl->length;
		}
		assert(outPos == postingCount);
		delete list;
		return new PostingList(postings, postingCount, false, true);
	} // end if (allPL)

	if (shifted)
		return list;

	ExtentList *result = list;
	if (list->listCount == 1) {
		result = list->lists[0];
		list->listCount = 0;
		delete list;
	}
	else if (someSPL) {
		int segmentCount = 0;
		int listCount = 0;
		for (int i = 0; i < list->listCount; i++) {
			if (list->lists[i]->getType() != ExtentList::TYPE_SEGMENTEDPOSTINGLIST)
				break;
			SegmentedPostingList *spl = (SegmentedPostingList*)list->lists[i];
			if (spl->inMemorySegments != NULL)
				break;
			segmentCount += spl->segmentCount;
			listCount++;
		}
		if (listCount >= 2) {
			SegmentedPostingList *spl =
				(SegmentedPostingList*)combineSegmentedPostingLists(list->lists, listCount);
			assert(spl != NULL);
			for (int i = 0; i < listCount; i++)
				delete list->lists[i];
			if (listCount == list->listCount) {
				list->listCount = 0;
				delete list;
				result = spl;
			}
			else {
				list->lists[0] = spl;
				list->firstStart[0] = list->firstEnd[0] = spl->firstPosting;
				list->lastStart[0] = list->lastEnd[0] = spl->lastPosting;
				for (int i = 0; i < list->listCount - listCount; i++) {
					list->lists[1 + i] = list->lists[listCount + i];
					list->firstStart[1 + i] = list->firstStart[listCount + i];
					list->firstEnd[1 + i] = list->firstEnd[listCount + i];
					list->lastStart[1 + i] = list->lastStart[listCount + i];
					list->lastEnd[1 + i] = list->lastEnd[listCount + i];
				}
				result = new ExtentList_OrderedCombination(list->lists, list->listCount - listCount + 1);
				list->listCount = 0;
				list->lists = NULL;
				delete list;
			}
		}
	}
	return result;
} // end of simplify_OrderedCombination(ExtentList_OrderedCombination*)


ExtentList * Simplifier::simplify_OR(ExtentList_OR *list) {
	bool orFound = false;
	int outPos = 0;
	for (int i = 0; i < list->elemCount; i++) {
		list->elem[i] = simplifyList(list->elem[i]);
		int type = list->elem[i]->getType();
		if (type == ExtentList::TYPE_EXTENTLIST_EMPTY)
			delete list->elem[i];
		else {
			list->elem[outPos++] = list->elem[i];
			if (type == ExtentList::TYPE_EXTENTLIST_OR)
				orFound = true;
		}
	}
	list->elemCount = outPos;

	if (list->elemCount <= 1) {
		ExtentList *result = (list->elemCount == 0 ? new ExtentList_Empty() : list->elem[0]);
		list->elemCount = 0;
		delete list;
		return result;
	}

	if (orFound) {
		// here we have the situation that one of the sub-lists is an ExtentList_OR
		// instance itself; fold into current list
		int allocated = 32, used = 0;
		ExtentList **lists = typed_malloc(ExtentList*, allocated);
		for (int i = 0; i < list->elemCount; i++) {
			if (list->elem[i]->getType() == ExtentList::TYPE_EXTENTLIST_OR) {
				ExtentList_OR *orList = (ExtentList_OR*)list->elem[i];
				for (int k = 0; k < orList->elemCount; k++) {
					if (used >= allocated)
						lists = typed_realloc(ExtentList*, lists, (allocated = allocated * 2));
					lists[used++] = orList->elem[k];
				}
				orList->elemCount = 0;
				delete orList;
			}
			else {
				if (used >= allocated)
					lists = typed_realloc(ExtentList*, lists, (allocated = allocated * 2));
				lists[used++] = list->elem[i];
			}
		}
		free(list->elem);
		list->elem = lists;
		list->elemCount = used;
	} // end if (orFound)

	bool onlyPostings = true;
	for (int i = 0; i < list->elemCount; i++) {
		int type = list->elem[i]->getType();
		if ((type != ExtentList::TYPE_POSTINGLIST) && (type != ExtentList::TYPE_SEGMENTEDPOSTINGLIST))
			onlyPostings = false;
	}
	if (onlyPostings) {
		ExtentList *result = new ExtentList_OR_Postings(list->elem, list->elemCount);
		result->alreadyOptimized = list->alreadyOptimized;
		list->elemCount = 0;
		list->elem = NULL;
		delete list;
		return result;
	}

	return list;
} // end of simplify_OR(ExtentList_OR*)


ExtentList * Simplifier::simplify_AND(ExtentList_AND *list) {
	bool andFound = false;
	for (int i = 0; i < list->elemCount; i++) {
		list->elem[i] = simplifyList(list->elem[i]);
		int type = list->elem[i]->getType();
		if (type == ExtentList::TYPE_EXTENTLIST_EMPTY) {
			delete list;
			return new ExtentList_Empty();
		}
		else if (type == ExtentList::TYPE_EXTENTLIST_AND)
			andFound = true;
	}

	if (list->elemCount <= 1) {
		ExtentList *result = (list->elemCount == 0 ? new ExtentList_Empty() : list->elem[0]);
		list->elemCount = 0;
		delete list;
		return result;
	}

	if (andFound) {
		// here we have the situation that one of the sub-lists is an ExtentList_AND
		// instance itself; fold into current list
		int allocated = 32, used = 0;
		ExtentList **lists = typed_malloc(ExtentList*, allocated);
		for (int i = 0; i < list->elemCount; i++) {
			if (list->elem[i]->getType() == ExtentList::TYPE_EXTENTLIST_AND) {
				ExtentList_AND *andList = (ExtentList_AND*)list->elem[i];
				for (int k = 0; k < andList->elemCount; k++) {
					if (used >= allocated)
						lists = typed_realloc(ExtentList*, lists, (allocated = allocated * 2));
					lists[used++] = andList->elem[k];
				}
				andList->elemCount = 0;
				delete andList;
			}
			else {
				if (used >= allocated)
					lists = typed_realloc(ExtentList*, lists, (allocated = allocated * 2));
				lists[used++] = list->elem[i];
			}
		}
		free(list->elem);
		list->elem = lists;
		list->elemCount = used;
	} // end if (andFound)

	return list;
} // end of simplify_AND(ExtentList_AND*)


ExtentList * Simplifier::simplify_Containment(ExtentList_Containment *list) {
	offset cStart, cEnd, firstStart, firstEnd, lastStart, lastEnd;

	list->container = simplifyList(list->container);
	list->containee = simplifyList(list->containee);

	if ((list->returnContainer) && (list->container->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)) {
		delete list;
		return new ExtentList_Empty();
	}
	else if ((!list->returnContainer) && (list->containee->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)) {
		delete list;
		return new ExtentList_Empty();
	}
	else if (list->inverted)
		return list;
	else if ((list->containee->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) ||
			(list->container->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)) {
		delete list;
		return new ExtentList_Empty();
	}
	else if (list->returnContainer) {
		offset containerWidth = 0, containeeWidth = MAX_OFFSET;
		if (list->containee->getType() == ExtentList::TYPE_EXTENTLIST_RANGE)
			containeeWidth = ((ExtentList_Range*)list->containee)->width;
		switch (list->container->getType()) {
			case ExtentList::TYPE_POSTINGLIST:
				containerWidth = 1;
				break;
			case ExtentList::TYPE_SEGMENTEDPOSTINGLIST:
				containerWidth = 1;
				break;
			case ExtentList::TYPE_EXTENTLIST_SEQUENCE:
				containerWidth = ((ExtentList_Sequence*)list->container)->elemCount;
				break;
			case ExtentList::TYPE_EXTENTLIST_FROMTO:
				containerWidth = 2;
				break;
		}
		if (containerWidth >= containeeWidth) {
			ExtentList *result = list->container;
			list->container = NULL;
			delete list;
			return result;
		}
		return list;
	}
	else if (!list->container->getFirstStartBiggerEq(0, &cStart, &cEnd)) {
		delete list;
		return new ExtentList_Empty();
	}
	else if (!list->containee->getFirstStartBiggerEq(0, &firstStart, &firstEnd)) {
		delete list;
		return new ExtentList_Empty();
	}
	else if (!list->containee->getLastEndSmallerEq(MAX_OFFSET, &lastStart, &lastEnd)) {
		delete list;
		return new ExtentList_Empty();
	}
	else if ((firstStart >= cStart) && (lastEnd <= cEnd)) {
		ExtentList *result = list->containee;
		list->containee = NULL;
		delete list;
		return result;
	}
	else
		return list;
} // end of simplify_Containment(ExtentList_Containment*)


ExtentList * Simplifier::simplify_Sequence(ExtentList_Sequence *list) {
	if (list->elemCount == 0) {
		delete list;
		return new ExtentList_Empty();
	}

	for (int i = 0; i < list->elemCount; i++) {
		list->elem[i] = simplifyList(list->elem[i]);
		if (list->elem[i]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
			delete list;
			return new ExtentList_Empty();
		}
	}

	if (list->elemCount == 1) {
		ExtentList *result = list->elem[0];
		list->elemCount = 0;
		delete list;
		return result;
	}

	return list;
} // end of simplify_Sequence(ExtentList_Sequence*)


