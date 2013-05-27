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
 * Implementation of the Ponte-Croft language-model-based retrieval function.
 *
 * author: Stefan Buettcher
 * created: 2006-01-23
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "ponte_croft.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


void PonteCroft::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	ok = false;
} // end of initialize(...)


PonteCroft::PonteCroft(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of PonteCroft(...)


PonteCroft::PonteCroft(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of PonteCroft(...)


PonteCroft::~PonteCroft() {
} // end of ~PonteCroft()


void PonteCroft::processCoreQuery() {
	offset start, end;
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
	avgDocLen = corpusSize / documentCount;
	for (int i = 0; i < elementCount; i++)
		if (df[i] > 0) {
			avgDensity[i] /= df[i];
			avgTF[i] /= df[i];
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
		candidate.from = start;
		candidate.to = end;
		candidate.score = 1.0;

		double docLen = (end - start + 1);

		// compute document score
#if 1
		for (int i = 0; i < elementCount; i++) {
			int tf = elementLists[i]->getCount(start, end);
			double density = tf / docLen;
			if (tf == 0)
				candidate.score *= (freq[i] / corpusSize);
			else {
				double p_ml = tf / docLen;
				double p_avg = avgDensity[i];
				double reliability =
					1.0 / (1.0 + avgTF[i]) * pow(avgTF[i] / (1.0 + avgTF[i]), tf);
				candidate.score *= pow(p_ml, 1 - reliability) * pow(p_avg, reliability);
			}
		}
		candidate.score = candidate.score * 100.0 * pow(10.0, elementCount);
#else
		long double score = 0.0;
		for (int i = 0; i < elementCount; i++) {
			int tf = elementLists[i]->getCount(start, end);
			double density = tf / docLen;
			if (tf == 0)
				score -= log(1 - df[i] / documentCount);
			else {
				score -= log(df[i] / documentCount);
				score -= density / avgDensity[i] * log(avgDensity[i]);
			}
		}
		candidate.score = score;
#endif

		// add current candidate to set of top-k results
		if (candidate.score > 0)
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


void PonteCroft::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
} // end of processModifiers(char**)


