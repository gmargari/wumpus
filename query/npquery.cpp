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
 * created: 2006-07-09
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "npquery.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


const double NPQuery::DEFAULT_B;
const double NPQuery::DEFAULT_K1;
const double NPQuery::DEFAULT_DECAY;


void NPQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;

	getConfigurationDouble("OKAPI_K1", &k1, DEFAULT_K1);
	getConfigurationDouble("OKAPI_B", &b, DEFAULT_B);
	processModifiers(modifiers);

	queryString = duplicateString(body);
	ok = false;
} // end of initialize(...)


NPQuery::NPQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of NPQuery(...)


NPQuery::NPQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of NPQuery(...)


NPQuery::~NPQuery() {
} // end of ~NPQuery()


#define MIN(a, b) (a < b ? a : b)


void NPQuery::processCoreQuery() {
	offset positiveContainerCount[MAX_SCORER_COUNT];
	ExtentList *elementLists[MAX_SCORER_COUNT];

	ExtentList *containerList = containerQuery->getResult();
	ExtentList *statisticsList = statisticsQuery->getResult();

	// compute statistics; this implementation is really slow, but efficiency
	// does not matter -- it's all experimental anyway
	offset containerCount = statisticsList->getLength();
	double averageContainerLength =
		(1.0 * statisticsList->getTotalSize()) / MAX(containerCount, 1);
	if (containerCount == 0) {
		// no matching container found: stop execution
		count = 0;
		return;
	}

	// compute the BM25 term weight for all elements
	double totalWeight = 0;
	for (int i = 0; i < elementCount; i++) {
		ExtentList *list = elementLists[i] = elementQueries[i]->getResult();
		ExtentList *containedIn =
			new ExtentList_Containment(new ExtentList_Copy(statisticsList),
					new ExtentList_Copy(list), true, false);
		offset df = containedIn->getLength();
		if ((df < 1) || (df > containerCount - 1))
			internalWeights[i] = 0;
		else
			internalWeights[i] = externalWeights[i] * log(containerCount / df);
		delete containedIn;
		totalWeight += internalWeights[i];
	}

	// initialize heap structure
	ScoredExtent candidate;
	results = typed_malloc(ScoredExtent, count + 1);
	int resultCount = 0;

	offset termOffsets[65536];
	int lastPos[MAX_SCORER_COUNT];
	int cnt[MAX_SCORER_COUNT];
	double bestDistance[MAX_SCORER_COUNT][MAX_SCORER_COUNT];
	double distanceScore[MAX_SCORER_COUNT][MAX_SCORER_COUNT];
	double proximityScore[MAX_SCORER_COUNT];
	offset first[1024], last[1024];
	offset nextOccurrence[MAX_SCORER_COUNT];
	memset(nextOccurrence, 0, sizeof(nextOccurrence));

	static const double PROXI_WEIGHT = 0.2;

	offset start = 0, end = 0;
	while (containerList->getFirstEndBiggerEq(end + 1, &start, &end)) {
		candidate.from = start;
		candidate.to = end;
		candidate.score = 0.0;

		double containerLength = (end - start + 1);
		double K = k1 * ((1 - b) + b * containerLength / averageContainerLength);

		// get all matches for this document
		int matchCnt = 0;
		int termCnt = 0;
		double maxScorePossible = 0;
		offset nextPossible = MAX_OFFSET;
		for (int i = 0; i < elementCount; i++) {
			lastPos[i] = -999;
			cnt[i] = 0;
			if (nextOccurrence[i] <= end) {
				offset n = elementLists[i]->getNextN(start, end, 1024, first, last);
				if (n > 0) {
					termCnt++;
					maxScorePossible += internalWeights[i] * (k1 + 1) * (1 + PROXI_WEIGHT);
				}
				for (int k = 0; k < n; k++)
					if (matchCnt < 65536)
						termOffsets[matchCnt++] = ((first[k] - start) << 8) + i;
				if (!elementLists[i]->getFirstStartBiggerEq(end + 1, &nextOccurrence[i], last))
					nextOccurrence[i] = MAX_OFFSET;
			}
			if (nextOccurrence[i] < nextPossible)
				nextPossible = nextOccurrence[i];
		}
		if (nextPossible > end)
			end = nextPossible - 1;

		if (matchCnt == 0)
			continue;
		if ((resultCount >= count) && (maxScorePossible < results[0].score))
			continue;

		if (termCnt == 1) {
			int term = termOffsets[0] & 255;
			candidate.score = internalWeights[term] * (k1 + 1) * matchCnt / (K + matchCnt);
			double proxiScore = internalWeights[term];
			candidate.score += PROXI_WEIGHT * internalWeights[term] * proxiScore / totalWeight;
		}
		else {
			sortOffsetsAscending(termOffsets, matchCnt);

			for (int j = 0; j < elementCount; j++) {
				for (int k = 0; k < elementCount; k++) {
					bestDistance[j][k] = 1E9;
					distanceScore[j][j] = 0;
				}
				bestDistance[j][j] = 1;
				distanceScore[j][j] = 1;
			}

			// compute TF values and proximity information
			lastPos[termOffsets[0] & 255] = 0;
			cnt[termOffsets[0] & 255]++;
			for (int i = 1; i < matchCnt; i++) {
				int term = (termOffsets[i] & 255);
				offset off = (termOffsets[i] >> 8);
				cnt[term]++;
				if (lastPos[term] == i - 1) {
					lastPos[term] = i;
					continue;
				}
				for (int k = 0; k < elementCount; k++) {
					if (lastPos[k] > lastPos[term]) {
						int termDistance = off - (termOffsets[lastPos[k]] >> 8);
						int queryTermDistance = i - lastPos[k];
						int realDistance = 1 + termDistance - queryTermDistance;
						if (k > term)
							realDistance++;
						assert(realDistance > 0);
						if (realDistance < bestDistance[term][k])
							bestDistance[term][k] = bestDistance[k][term] = realDistance;
						distanceScore[term][k] += pow(realDistance, -decay);
						distanceScore[k][term] += pow(realDistance, -decay);
					}
				}
				lastPos[term] = i;
			}

			candidate.score = 0.0;
			for (int i = 0; i < elementCount; i++)
				if (cnt[i] > 0) {
					candidate.score +=
						internalWeights[i] * (k1 + 1) * cnt[i] / (K + cnt[i]);
					double proxiScore = 0.0;
#if 0
					for (int k = 0; k < elementCount; k++)
						proxiScore += internalWeights[k] / pow(bestDistance[i][k], decay);
					assert(proxiScore >= 0.0);
					candidate.score +=
						PROXI_WEIGHT * internalWeights[i] * proxiScore / totalWeight;
#else
					for (int k = 0; k < elementCount; k++)
						proxiScore += internalWeights[k] * MIN(1, distanceScore[i][k]);
					candidate.score +=
						PROXI_WEIGHT * internalWeights[i] * proxiScore / totalWeight;
#endif
				}
			assert(candidate.score >= 0.0);
		}

		// add current candidate to set of top-k results
		if (candidate.score > 0)
			addToResultSet(&candidate, &resultCount);
	} // end while (containerList->getFirstEndBiggerEq(end + 1, &start, &end))

	count = resultCount;
} // end of processCoreQuery()


void NPQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	k1 = getModifierDouble(modifiers, "k1", k1);
	b = getModifierDouble(modifiers, "b", b);
	decay = getModifierDouble(modifiers, "decay", DEFAULT_DECAY);
} // end of processModifiers(char**)


