/**
 * Copyright (C) 2008 Stefan Buettcher. All rights reserved.
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
 * created: 2008-07-03
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "bm25f_query.h"
#include "bm25query.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


void BM25FQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	actualQuery = this;
	ok = false;
} // end of initialize(...)


BM25FQuery::BM25FQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of BM25FQuery(...)


BM25FQuery::BM25FQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of BM25FQuery(...)


BM25FQuery::~BM25FQuery() {
	if (fieldList != NULL) {
		delete fieldList;
		fieldList = NULL;
	}
} // end of ~BM25FQuery()


void BM25FQuery::processCoreQuery() {
	offset start, end;
	double df[MAX_SCORER_COUNT];
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();

	offset documentCount = containerList->getLength();
	if (documentCount == 0) {
		// no matching container found: stop execution
		count = 0;
		return;
	}
	double avgDocLen = containerList->getTotalSize() * 1.0 / documentCount;

	// Compute IDF weights for all query terms.
	for (int i = 0; i < elementCount; i++) {
		elementLists[i] = elementQueries[i]->getResult();
		df[i] = ExtentList_Containment(new ExtentList_Copy(containerList),
				new ExtentList_Copy(elementLists[i]), true, false).getLength();
		internalWeights[i] = externalWeights[i] * log(documentCount / df[i]);
	}

	// Compute the average length of the field within each document. We
	// subtract 2 from the field length to account for the opening and
	// closing XML tags. We don't do the same for the body, because presumably
	// the body is large enough so that such small differences don't matter.
  ExtentList_Containment containedFieldList(
			new ExtentList_Copy(containerList), new ExtentList_Copy(fieldList),
			false, false);
	double avgFieldLen =
		(containedFieldList.getTotalSize() - 2 * containedFieldList.getLength())
		* 1.0 / documentCount + 1E-3;
	assert(avgFieldLen > 0);
	double avgBodyLen =
		(containerList->getTotalSize() - containedFieldList.getTotalSize())
		* 1.0 / documentCount + 1E-3;

	// initialize heap structure
	ScoredExtent candidate;
	results = typed_malloc(ScoredExtent, count + 1);
	int resultCount = 0;

	// prune the search by only looking at documents that might contain
	// interesting information; "nextOffsetPossible" contains the next
	// index position at which we can find a query element
	offset nextOffsetPossible = MAX_OFFSET;
	offset elemStart, elemEnd;
	for (int elem = 0; elem < elementCount; elem++)
		if (elementLists[elem]->getFirstEndBiggerEq(0, &elemStart, &elemEnd))
			if (elemEnd < nextOffsetPossible)
				nextOffsetPossible = elemEnd;

	while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end)) {
		candidate.from = start;
		candidate.to = end;
		candidate.score = 0;

		// Find the first occurrence of "<field>".."</field>" in the document.
		// Ignore all additional occurrences. Set fieldLen and bodyLen accordingly.
		offset fieldStart = -1, fieldEnd = -1;
		offset fieldLen = 0, bodyLen = (end - start + 1);
		if (fieldList->getFirstStartBiggerEq(start, &fieldStart, &fieldEnd)) {
			fieldLen = (fieldEnd - fieldStart - 1);
			bodyLen -= (fieldEnd - fieldStart + 1);
		}

		// compute current document's score
		for (int i = 0; i < elementCount; i++) {
			int tfInField = 0, tfInBody = 0;

			offset s = start - 1, e;
			while (elementLists[i]->getFirstStartBiggerEq(s + 1, &s, &e)) {
				if (e > end)
					break;
				if ((s >= fieldStart) && (e <= fieldEnd))
					tfInField++;
				else
					tfInBody++;
			}
			if (tfInField + tfInBody == 0)
				continue;

			// Compute the current document's score.
			double adjustedTFInField = tfInField / (1.0 - b1 + b1 * fieldLen / avgFieldLen);
			double adjustedTFInBody = tfInBody / (1.0 - b2 + b2 * bodyLen / avgBodyLen);
			double tf = w * adjustedTFInField + 1.0 * adjustedTFInBody;
			double score = internalWeights[i] * tf * (k1 + 1) / (k1 + tf);
			candidate.score += score;
		}

		if (candidate.score > 1E-9) {
			// add candidate to top-k result set
			addToResultSet(&candidate, &resultCount);
		}

		// find next document that could possibly contain one of the query terms
		nextOffsetPossible = MAX_OFFSET;
		for (int elem = 0; elem < elementCount; elem++)
			if (elementLists[elem]->getFirstEndBiggerEq(start + 1, &elemStart, &elemEnd))
				if (elemEnd < nextOffsetPossible)
					nextOffsetPossible = elemEnd;
		if (nextOffsetPossible < end + 1)
			nextOffsetPossible = end + 1;
	} // end while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end))

	count = resultCount;
} // end of processCoreQuery()


void BM25FQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	char *s = getModifierString(modifiers, "field", "title");
  toLowerCase(s);
	char *gclExpression = (char*)malloc(16 + 2 * strlen(s));
	sprintf(gclExpression, "\"<%s>\"..\"</%s>\"", s, s);
	fieldList = getListForGCLExpression(gclExpression);
	free(gclExpression);
	free(s);
	w = getModifierDouble(modifiers, "w", 2.0);
	k1 = getModifierDouble(modifiers, "k1", BM25Query::DEFAULT_K1);
	b1 = getModifierDouble(modifiers, "b1", BM25Query::DEFAULT_B);
	b2 = getModifierDouble(modifiers, "b2", BM25Query::DEFAULT_B);
} // end of processModifiers(char**)


