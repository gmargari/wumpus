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
 * created: 2007-03-26
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "experimental_query.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


void ExperimentalQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	actualQuery = this;
	ok = false;
} // end of initialize(...)


ExperimentalQuery::ExperimentalQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of ExperimentalQuery(...)


ExperimentalQuery::ExperimentalQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of ExperimentalQuery(...)


ExperimentalQuery::~ExperimentalQuery() {
} // end of ~ExperimentalQuery()


void ExperimentalQuery::processCoreQuery() {
	offset start, end, s, e;
	double freq[MAX_SCORER_COUNT], df[MAX_SCORER_COUNT];
	double avgTF[MAX_SCORER_COUNT], avgDensity[MAX_SCORER_COUNT];
	offset docPreviewStart[32], docPreviewEnd[32];
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();
	ExtentList *statisticsList = statisticsQuery->getResult();

	// compute containerCount and avgContainerLength;
	// count positive and negative containers for all query elements
	offset documentCount = 0;
	double avgDocLen = 0;
	double corpusSize = 0;

	// compute collection statistics and term probability distributions
	for (int i = 0; i < elementCount; i++) {
		freq[i] = df[i] = avgTF[i] = avgDensity[i] = 0;
		elementLists[i] = elementQueries[i]->getResult();
	}
	int previewSize =
		statisticsQuery->getResult()->getNextN(0, MAX_OFFSET, 32, docPreviewStart, docPreviewEnd);
	while (previewSize > 0) {
		documentCount += previewSize;
		for (int i = 0; i < previewSize; i++) {
			double docLen = (docPreviewEnd[i] - docPreviewStart[i] + 1);
			corpusSize += docLen;
			for (int t = 0; t < elementCount; t++) {
				offset tf = elementLists[t]->getCount(docPreviewStart[i], docPreviewEnd[i]);
				if (tf > 0) {
					freq[t] += tf;
					df[t]++;
					avgTF[t] += tf;
					avgDensity[t] += tf / docLen;
				}
			}
		}
		previewSize = statisticsQuery->getResult()->getNextN(
				docPreviewStart[previewSize - 1] + 1, MAX_OFFSET, 32, docPreviewStart, docPreviewEnd);
	} // end while (previewSize > 0)

	double p_global[MAX_SCORER_COUNT], p_local[MAX_SCORER_COUNT];
	double totalTermWeight = 0;
	avgDocLen = corpusSize / documentCount;
	for (int i = 0; i < elementCount; i++) {
		if (df[i] > 0) {
			avgDensity[i] /= df[i];
			avgTF[i] /= df[i];
		}
		if (freq[i] == 0)
			freq[i] = corpusSize - 1;
		p_global[i] = (freq[i] + 0.5) / corpusSize;
		totalTermWeight -= externalWeights[i] * log(p_global[i]);
	}

	if (documentCount == 0) {
		// no matching container found: stop execution
		count = 0;
		return;
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
		candidate.score = 1000;

		// compute document score according to the query being generated by the
		// current document's language model
		int matchCnt = 0;
		for (int i = 0; i < elementCount; i++) {
			double tf = elementLists[i]->getCount(start, end);
			p_local[i] = tf / docLen;
			double p_smoothed = (tf + dirichletMu * p_global[i]) / (docLen + dirichletMu);
			candidate.score += externalWeights[i] * log(p_smoothed);
			if (tf > 0.5)
				matchCnt++;
		}

		// if more than 1 query term found, adjust score based on term proximity
		if (matchCnt > 1) {
			candidate.score = 1000;
			for (int j = 0; j < elementCount; j++) {
				offset s_j, e_j;
				double weightSum = 0;
				double probSum = 1;
				for (int k = 0; k < elementCount; k++) {
					double minDelta = 1E9;
					if (k != j) {
						offset s_k = start - 1, e_k;
						while (elementLists[k]->getFirstStartBiggerEq(s_k + 1, &s_k, &e_k)) {
							if (e_k > end)
								break;
							if (elementLists[j]->getFirstStartBiggerEq(e_k + 1, &s_j, &e_j))
								if (e_j <= end)
									minDelta = MIN(minDelta, s_j - e_k);
							if (elementLists[j]->getLastEndSmallerEq(s_k - 1, &s_j, &e_j))
								if (s_j >= start)
									minDelta = MIN(minDelta, s_k - e_j);
						}
					}
					assert(minDelta > 0);
					double p = MAX(0.5 / minDelta, p_local[j]);
					double weight = -log(p_global[k]);
					weightSum += weight;
					probSum += weight * log(p);
				}
				probSum = exp(probSum / weightSum);
				p_local[j] = MIN(probSum, 2 * p_local[j]);
				double p_smoothed =
					(p_local[j] * docLen + dirichletMu * p_global[j]) / (docLen + dirichletMu);
				candidate.score += externalWeights[j] * log(p_smoothed);
			}
		} // end if (matchCnt > 1)

		// add current candidate to result set
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


void ExperimentalQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	dirichletMu = getModifierDouble(modifiers, "mu", 2000);
} // end of processModifiers(char**)


