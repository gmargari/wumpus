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
 * created: 2007-04-16
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "divergence_query.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


const int DivergenceQuery::METHOD_GB2;
const int DivergenceQuery::METHOD_IFB2;


void DivergenceQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	actualQuery = this;
	method = METHOD_IFB2;
	ok = false;
} // end of initialize(...)


DivergenceQuery::DivergenceQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of DivergenceQuery(...)


DivergenceQuery::DivergenceQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of DivergenceQuery(...)


DivergenceQuery::~DivergenceQuery() {
} // end of ~DivergenceQuery()


void DivergenceQuery::processCoreQuery() {
	offset start, end;
	double freq[MAX_SCORER_COUNT], df[MAX_SCORER_COUNT];
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();

	offset documentCount = containerList->getLength();
	if (documentCount == 0) {
		// no matching container found: stop execution
		count = 0;
		return;
	}
	double avgDocLen = containerList->getTotalSize() * 1.0 / documentCount;

	for (int i = 0; i < elementCount; i++) {
		elementLists[i] = elementQueries[i]->getResult();
		freq[i] = ExtentList_Containment(new ExtentList_Copy(containerList),
				new ExtentList_Copy(elementLists[i]), false, false).getLength();
		df[i] = ExtentList_Containment(new ExtentList_Copy(containerList),
				new ExtentList_Copy(elementLists[i]), true, false).getLength();
		internalWeights[i] = externalWeights[i];
	}

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
		double docLen = (end - start + 1);
		candidate.from = start;
		candidate.to = end;
		candidate.score = 0;

		// compute current document's score
		for (int i = 0; i < elementCount; i++) {
			double tf = elementLists[i]->getCount(start, end);
			if (tf > 0) {
				// The following line is Equation 42 in Amati's paper.
				double tfn = tf * log2(1 + avgDocLen / docLen);
				double inf1 = 0.0;
				switch (method) {
					case METHOD_GB2: {
							// The following line is Equation 17 in Amati's paper.
							double lambda = freq[i] * 1.0 / documentCount;
							inf1 = -log2(1 / (1 + lambda)) - tfn * log2(lambda / (1 + lambda));
						}
						break;
					case METHOD_IFB2:
						// The following lines are Equation 21 in Amati's paper.
						if (freq[i] < documentCount)
							inf1 = tfn * log2((documentCount + 1.0) / (freq[i] + 0.5));
						else
							inf1 = 0.0;
						break;
				}
				// The following line is Equation 27 in Amati's paper ("normalization B").
				double score = (freq[i] + 1.0) / (df[i] * (tfn + 1.0)) * inf1 * internalWeights[i];
				candidate.score += score;
			}
		}

		// add candidate to top-k result set
		addToResultSet(&candidate, &resultCount);

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


void DivergenceQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	char *s = getModifierString(modifiers, "method", "ifb2");
	if (strcasecmp(s, "ifb2") == 0)
		method = METHOD_IFB2;
	else if (strcasecmp(s, "gb2") == 0)
		method = METHOD_GB2;
	free(s);
} // end of processModifiers(char**)


