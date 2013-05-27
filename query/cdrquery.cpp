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
 * Implementation of the @cdr query: Cover Density Ranking.
 *
 * Sort passages within document by length (ascending). Put decay in front
 * of every passage score (decay=0.5). If length of passage < KAPPA, score
 * is WEIGHT, else WEIGHT / (KAPPA / len).
 *
 * author: Stefan Buettcher
 * created: 2005-10-12
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <math.h>
#include <string.h>
#include "cdrquery.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../misc/all.h"


static const char * LOG_ID = "CDRQuery";

const double CDRQuery::DEFAULT_K;
const int CDRQuery::CDR_MAX_SCORER_COUNT;
const int CDRQuery::DEFAULT_MAX_LEVEL;


void CDRQuery::initialize(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	actualQuery = this;
	ok = false;
} // end of initialize(Index*, char*, char**, char*, VisibleExtents*, int)


CDRQuery::CDRQuery() {
	mustFreeVisibleExtentsInDestructor = false;
} // end of CDRQuery()


CDRQuery::CDRQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of CDRQuery(...)


CDRQuery::CDRQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of CDRQuery(...)


CDRQuery::~CDRQuery() {
} // end of ~CDRQuery()


bool CDRQuery::parse() {
	// if no container is given, assume default container
	char defaultContainer[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue("DEFAULT_RETRIEVAL_SET", defaultContainer))
		strcpy(defaultContainer, DOC_QUERY);

	if (!parseQueryString(queryString, defaultContainer, NULL, memoryLimit)) {
		syntaxErrorDetected = finished = true;
		ok = false;
	}
	else if (((elementCount < 1) || (elementCount > CDR_MAX_SCORER_COUNT)) && (maxLevel > 1)) {
		// @cdr queries may not contain more than CDR_MAX_SCORER_COUNT query terms
		syntaxErrorDetected = finished = true;
		ok = false;
	}
	else {
		if (statisticsQuery == NULL)
			if (visibleExtents != NULL)
				statisticsQuery = new GCLQuery(index, visibleExtents->getExtentList());
		processQuery();
		ok = true;
	}
	return ok;			
} // end of parse()


void CDRQuery::processCoreQuery() {
	offset start, end, s, e;
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();
	ExtentList *statisticsList = statisticsQuery->getResult();

	if ((containerList->getLength() == 0) || (elementCount < 1)) {
		// no matching container found: stop execution
		count = 0;
		return;
	}

	if ((elementCount > CDR_MAX_SCORER_COUNT) && (maxLevel > 1)) {
		sprintf(errorMessage, "Too many scorers. Limit is: %d.", CDR_MAX_SCORER_COUNT);
		log(LOG_ERROR, LOG_ID, errorMessage);
		count = 0;
		return;
	}

	// compute query term weights
	for (int i = 0; i < elementCount; i++) {
		elementLists[i] = elementQueries[i]->getResult();
		if (maxLevel == 1)
			internalWeights[i] = 1;
		else {
			ExtentList_Containment list(statisticsList, elementLists[i], false, false);
			internalWeights[i] =
				MAX(0, externalWeights[i] * log(1E12 / list.getLength()));
			list.detachSubLists();
		}
	}

	// sort all possible combinations of query terms by the sum of
	// their respective weights
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < elementCount - 1; i++) {
			if (internalWeights[i + 1] < internalWeights[i]) {
				ExtentList *tmpList = elementLists[i];
				elementLists[i] = elementLists[i + 1];
				elementLists[i + 1] = tmpList;
				double tmpWeight = internalWeights[i];
				internalWeights[i] = internalWeights[i + 1];
				internalWeights[i + 1] = tmpWeight;
				changed = true;
			}
		}
	}

	bool inStrictMode = (maxLevel == 1);
	int strictSet = (1 << elementCount) - 1;
	float baseScoreForStrictMode = 10000.0 * strictSet;

	// construct subset queries
	ExtentList *subsets[1 << CDR_MAX_SCORER_COUNT];
	if (inStrictMode) {
		strictSet = 0;
		baseScoreForStrictMode = 0.0;
		ExtentList **lists = typed_malloc(ExtentList*, elementCount);
		memcpy(lists, elementLists, elementCount * sizeof(ExtentList*));
		subsets[strictSet] =
			new ExtentList_AND(lists, elementCount, ExtentList::DO_NOT_TAKE_OWNERSHIP);
	}
	else {
		for (int k = 0; k < (1 << elementCount); k++) {
			ExtentList **lists = typed_malloc(ExtentList*, elementCount);
			int listCnt = 0;
			for (int i = 0; i < elementCount; i++)
				if ((k & (1 << i)) || (k == 0))
					lists[listCnt++] = elementLists[i];
			assert(listCnt > 0);
			if (k == 0)
				subsets[k] = new ExtentList_OR(lists, listCnt, ExtentList::DO_NOT_TAKE_OWNERSHIP);
			else
				subsets[k] = new ExtentList_AND(lists, listCnt, ExtentList::DO_NOT_TAKE_OWNERSHIP);
		}
	}

	// construct the list we use to retrieve matching documents
	ExtentList *retrievalSet = subsets[0];
	ExtentList *retrievalList =
		new ExtentList_Containment(containerList, retrievalSet, true, false);

	// initialize heap structure
	ScoredExtent candidate;
	results = typed_malloc(ScoredExtent, count + 1);
	int resultCount = 0;

	// traverse list of matching documents
	start = -1;
	while (retrievalList->getFirstStartBiggerEq(start + 1, &start, &end)) {
		int whichSubset = strictSet;
		if (inStrictMode)
			candidate.score = baseScoreForStrictMode;
		else {
			whichSubset = 0;
			for (int i = 0; i < elementCount; i++)
				if (elementLists[i]->getFirstStartBiggerEq(start, &s, &e))
					if (e <= end)
						whichSubset += (1 << i);
			if (whichSubset < (1 << elementCount) - maxLevel)
				continue;
			candidate.score = 10000.0 * whichSubset;
			if ((resultCount >= count) && (results[0].score >= baseScoreForStrictMode)) {
				// at this point, we know that all top "count" results must contain all
				// the query terms; => switch from OR mode to AND mode
				retrievalList->detachSubLists();
				delete retrievalList;
				retrievalList =
					new ExtentList_Containment(containerList, subsets[strictSet], true, false);
				inStrictMode = true;
			}
		} // end else [!inStrictMode]

		candidate.from = start;
		candidate.to = end;
		float score = 0.0;
		s = start - 1;
		while (subsets[whichSubset]->getFirstStartBiggerEq(s + 1, &s, &e)) {
			if (e > end)
				break;
			score += 100.0 * MIN(1, K / (e - s + 1));
		}
		candidate.score += MIN(9999.9, score);
		addToResultSet(&candidate, &resultCount);
	} // end while (retrievalList->getFirstStartBiggerEq(start + 1, &start, &end))

	// delete retrievalList
	retrievalList->detachSubLists();
	delete retrievalList;

	// delete subset-lists
	if (maxLevel == 1) {
		delete subsets[0];
	}
	else {
		for (int k = 0; k < (1 << elementCount); k++)
			delete subsets[k];
	}

	count = resultCount;
} // end of processCoreQuery()


void CDRQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	K = MAX(1, getModifierDouble(modifiers, "k", DEFAULT_K));
	maxLevel = MAX(1, getModifierInt(modifiers, "maxlevel", DEFAULT_MAX_LEVEL));
	if (getModifierBool(modifiers, "strict", false))
		maxLevel = 1;
} // end of processModifiers(char**)


