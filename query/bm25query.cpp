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
 * Implementation of the generic Okapi BM25 scoring function.
 *
 * author: Stefan Buettcher
 * created: 2004-09-27
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "bm25query.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


const double BM25Query::DEFAULT_B;
const double BM25Query::DEFAULT_K1;


void BM25Query::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;

	getConfigurationDouble("OKAPI_K1", &k1, DEFAULT_K1);
	getConfigurationDouble("OKAPI_B", &b, DEFAULT_B);
	processModifiers(modifiers);

	queryString = duplicateString(body);
	actualQuery = this;
	ok = false;
} // end of initialize(...)


BM25Query::BM25Query(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of BM25Query(...)


BM25Query::BM25Query(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of BM25Query(...)


BM25Query::~BM25Query() {
} // end of ~BM25Query()


static const int PREVIEW = 64;

/**
 * The LHS ("ListHeapStruct") structure is used to "merge" the individual
 * document-level postings lists and process the resulting document stream.
 **/
typedef struct {
	int who;
	offset next;
	int previewPos;
	int previewCount;
	offset preview[PREVIEW];
} LHS;


static int lhsComparator(const void *a, const void *b) {
	LHS *x = *((LHS**)a);
	LHS *y = *((LHS**)b);
	offset difference = x->next - y->next;
	if (difference < 0)
		return -1;
	else if (difference > 0)
		return +1;
	else
		return 0;
} // end of lhsComparator(const void*, const void*)


#define MIN(a, b) (a < b ? a : b)


void BM25Query::processCoreQuery() {
	offset start, end, s, e;
	offset positiveContainerCount[MAX_SCORER_COUNT];
	offset nextPossibleForElement[MAX_SCORER_COUNT];
	ExtentList *elementLists[MAX_SCORER_COUNT];

	ExtentList *containerList = containerQuery->getResult();
	ExtentList *statisticsList = statisticsQuery->getResult();

	// compute containerCount and avgContainerLength;
	// count positive and negative containers for all query elements
	offset containerCount = 0;
	offset avgContainerLength = 0;

	offset nextOffsetPossible = MAX_OFFSET;
	for (int elem = 0; elem < elementCount; elem++) {
		elementLists[elem] = elementQueries[elem]->getResult();
		if (elementLists[elem]->getFirstEndBiggerEq(0, &start, &end)) {
			nextPossibleForElement[elem] = end;
			if (nextPossibleForElement[elem] < nextOffsetPossible)
				nextOffsetPossible = nextPossibleForElement[elem];
		}
		else
			nextPossibleForElement[elem] = MAX_OFFSET;
		positiveContainerCount[elem] = 0;
	} // end for (int elem = 0; elem < elementCount; elem++)

	// scan over all containers to compute collection statistics; use getNextN to
	// reduce the number of virtual method calls (getFirstStartBiggerEq)
	offset nextPossible = 0;
	offset containerPreviewStart[64], containerPreviewEnd[64];
	int previewSize = statisticsList->getNextN(0, MAX_OFFSET, 64,
				containerPreviewStart, containerPreviewEnd);
	int previewPos = 0;
	while (previewPos < previewSize) {
		start = containerPreviewStart[previewPos];
		end = containerPreviewEnd[previewPos];
		avgContainerLength += (end - start + 1);
		containerCount++;
		if (end >= nextPossible) {
			nextPossible = MAX_OFFSET;
			for (int elem = 0; elem < elementCount; elem++) {
				if (nextPossibleForElement[elem] <= end) {
					if (elementLists[elem]->getFirstStartBiggerEq(start, &s, &e)) {
						if (e <= end)
							positiveContainerCount[elem]++;
						else
							nextPossibleForElement[elem] = e;
					}
					else
						nextPossibleForElement[elem] = MAX_OFFSET;
				}
				if (nextPossibleForElement[elem] < nextPossible)
					nextPossible = nextPossibleForElement[elem];
			}
		}
		if (++previewPos >= previewSize) {
			if (previewSize == 64) {
				previewPos = 0;
				previewSize = statisticsList->getNextN(containerPreviewStart[previewSize - 1] + 1,
						MAX_OFFSET, 64, containerPreviewStart, containerPreviewEnd);
			}
		}
	} // end while (containerList->getFirstStartBiggerEq(containerPosition, &start, &end))

	if (containerCount == 0) {
		// no matching container found: stop execution
		count = 0;
		return;
	}

	double averageContainerLength = avgContainerLength;
	averageContainerLength /= containerCount;

	if (noIDF) {
		for (int i = 0; i < elementCount; i++)
			internalWeights[i] = externalWeights[i];
	}
	else {
		// compute the BM25 term weight for all elements
		for (int i = 0; i < elementCount; i++) {
			double df = positiveContainerCount[i];
			if ((df < 1) || (df > containerCount - 1))
				internalWeights[i] = 0;
			else
				internalWeights[i] = externalWeights[i] * log(containerCount / df);

			// make sure no expansion term can get a greater weight than the average
			// original query term
			if (i >= originalElementCount) {
				double avgOrigWeight = 0;
				for (int k = 0; k < originalElementCount; k++)
					avgOrigWeight += internalWeights[k] / originalElementCount;
				if (internalWeights[i] > avgOrigWeight)
					internalWeights[i] = avgOrigWeight;
			}
		}
	} // end else [!noIDF]

	if (verbose) {
		char *tempString = (char*)malloc(MAX_QUERY_LENGTH + elementCount * 32);
		int len = 0;
		for (int i = 0; i < elementCount; i++) {
			char *q = elementQueries[i]->getQueryString();
			len += sprintf(&tempString[len],
					"%s%s (%.4lf)", (i == 0 ? "" : ", "), q, internalWeights[i]);
			free(q);
		}
		addVerboseString("Term weights", tempString);
		free(tempString);
	} // end if (verbose)

	// adjust weights and find term with minimum weight (for MaxScore)
	int termWithMinWeight = 0;
	for (int i = 0; i < elementCount; i++) {
		// make sure every term gets at least internal weight 0.5; this way we
		// make sure that even very frequent terms, like "new" can have a reasonable
		// impact, which important because they are sometimes crucial, especially
		// when taking term proximity into account (e.g., "new jersey", "the doors")
		if (internalWeights[i] < 1) {
			if (internalWeights[i] < 0)
				internalWeights[i] = 0.5;
			else
				internalWeights[i] += 0.5 * (1 - internalWeights[i]);
		}
		if (internalWeights[i] < internalWeights[termWithMinWeight])
			termWithMinWeight = i;
	}
	double maxImpactOfMinWeightTerm = (k1 + 1) * (internalWeights[termWithMinWeight]);

	double weights[MAX_SCORER_COUNT];
	memcpy(weights, internalWeights, sizeof(weights));
	qsort(weights, elementCount, sizeof(double), doubleComparator);
	double proxiThreshold;
	if (elementCount < 3)
		proxiThreshold = weights[elementCount - 1] - 0.001;
	else
		proxiThreshold = weights[2] - 0.001;

	// local variables for term proximity scoring
	int whichScorer[MAX_SCORER_COUNT];
	unsigned int tf[MAX_SCORER_COUNT];
	float proxiScore[MAX_SCORER_COUNT];
	char *areTheSame = NULL;
	offset **occ = NULL;
	if (useTermProximity) {
		areTheSame = (char*)malloc(elementCount * elementCount);
		memset(areTheSame, 0, elementCount * elementCount);
		occ = typed_malloc(offset*, elementCount);
		for (int i = 0; i < elementCount; i++) {
			occ[i] = typed_malloc(offset, PREVIEW);
			proxiScore[i] = 0.0;
			areTheSame[i * elementCount + i] = 1;
		}
	}
	offset *occPos[MAX_SCORER_COUNT];

	// initialize heap structure
	ScoredExtent candidate;
	results = typed_malloc(ScoredExtent, count + 1);
	int resultCount = 0;

	// prune the search by only looking at documents that might contain
	// interesting information; "nextOffsetPossible" contains the next
	// index position at which we can find a query element
	nextOffsetPossible = MAX_OFFSET;
	offset elemStart, elemEnd;
	for (int elem = 0; elem < elementCount; elem++) {
		if (elementLists[elem]->getFirstEndBiggerEq(0, &elemStart, &elemEnd)) {
			nextPossibleForElement[elem] = elemEnd;
			if (nextPossibleForElement[elem] < nextOffsetPossible)
				nextOffsetPossible = nextPossibleForElement[elem];
		}
		else
			nextPossibleForElement[elem] = MAX_OFFSET;
	}

	while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end)) {
		candidate.from = start;
		candidate.to = end;
		candidate.score = 0.0;

		double containerLength = (end - start + 1);
		double K = k1 * ((1 - b) + b * containerLength / averageContainerLength);
		int scorersInCurrentDocument = 0;

		// compute BM25 score for this document
		for (int i = 0; i < elementCount; i++) {
			if (nextPossibleForElement[i] > end)
				continue;

			ExtentList *elemList = elementLists[i];
			unsigned int termFrequency = elemList->getCount(start, end);
			if (termFrequency > 0) {
				if (noTF)
					termFrequency = 1;
				candidate.score +=
					internalWeights[i] * (k1 + 1.0) * termFrequency / (K + termFrequency);

				if (chronologicalTermRank != 0) {
					// add score component according to chronological term rank;
					// see Adam Troy and Guo-Qiang Zhang, "Enhancing relevance scoring
					// with chronological term rank", SIGIR 2007, p. 599-606 for details.
					offset qs, qe;
					elemList->getFirstStartBiggerEq(candidate.from, &qs, &qe);
					double tr = qs - candidate.from;
					double dl = containerLength;
					double weight = internalWeights[i] * chronologicalTermRank;
					candidate.score += weight * (1 - log10(tr / 30 + 10) / log10(dl / 30 + 10));
				}

				tf[i] = termFrequency;
				whichScorer[scorersInCurrentDocument++] = i;
			} // end if (termFrequency > 0)

			if (!elemList->getFirstEndBiggerEq(end + 1, &nextPossibleForElement[i], &nextPossibleForElement[i]))
				nextPossibleForElement[i] = MAX_OFFSET;
		} // end for (int i = 0; i < elementCount; i++)
		assert(candidate.score >= 0.0);

		// update the value of "nextOffsetPossible"
		nextOffsetPossible = MAX_OFFSET;
		for (int i = 0; i < elementCount; i++) {
			if (i == termWithMinWeight)
				if ((resultCount >= count) && (maxImpactOfMinWeightTerm <= results[0].score))
					continue;
			if (nextPossibleForElement[i] < nextOffsetPossible)
				nextOffsetPossible = nextPossibleForElement[i];
		}
		if (nextOffsetPossible <= end)
			nextOffsetPossible = end + 1;

		// advance to next candidate if the score seen here is below any reasonable threshold
		if (candidate.score < 1E-9)
			continue;

		// perform term proximity scoring for all terms found in the current document
		if (useTermProximity) {
			if (scorersInCurrentDocument > 1) {
				for (int i = 0; i < scorersInCurrentDocument; i++) {
					offset dummy[PREVIEW];
					int who = whichScorer[i];
					if (tf[who] >= PREVIEW)
						tf[who] = PREVIEW - 1;
					elementLists[who]->getNextN(start, end, tf[who], occ[who], dummy);
					occ[who][tf[who]] = MAX_OFFSET;
					occPos[who] = occ[who];
				}
				int previousTerm = 0;
				offset previousPos = -1000;
				while (true) {
					int who, next;
					offset nextPos = MAX_OFFSET;
					for (int i = 0; i < scorersInCurrentDocument; i++) {
						who = whichScorer[i];
						if (occPos[who][0] < nextPos) {
							next = who;
							nextPos = occPos[who][0];
						}
					}
					if (nextPos >= MAX_OFFSET)
						break;
					who = next;
					if (!areTheSame[previousTerm * elementCount + who]) {
						float distance = (occPos[who][0] - previousPos);
						if (distance < 0.999) {
							areTheSame[previousTerm * elementCount + who] = 1;
							areTheSame[who * elementCount + previousTerm] = 1;
						}
						else {
#if 1
							if (internalWeights[previousTerm] >= proxiThreshold)
								proxiScore[who] += internalWeights[previousTerm] / pow(distance, 2.0);
							if (internalWeights[who] >= proxiThreshold)
								proxiScore[previousTerm] += internalWeights[who] / pow(distance, 2.0);
#else
							if (internalWeights[previousTerm] >= proxiThreshold)
								proxiScore[who] +=
									MIN(1, internalWeights[previousTerm] / internalWeights[who]) / pow(distance, 1.5);
							if (internalWeights[who] >= proxiThreshold)
								proxiScore[previousTerm] +=
									MIN(1, internalWeights[who] / internalWeights[previousTerm]) / pow(distance, 1.5);
#endif
						}
					}
					previousPos = occPos[who][0];
					previousTerm = who;
					occPos[who]++;
				} // end while (true)
			}
			for (int i = 0; i < scorersInCurrentDocument; i++) {
				int who = whichScorer[i];
#if 1
				candidate.score +=
					MIN(1.0, internalWeights[i]) * (k1 + 1.0) * proxiScore[who] / (K + proxiScore[who]);
#else
				candidate.score +=
					0.5 * internalWeights[who] * (k1 + 1.0) * proxiScore[who] / (K + proxiScore[who]);
#endif
				proxiScore[who] = tf[who] = 0;
			}
		} // end if (useTermProximity)

		// add current candidate to result set
		if (candidate.score > 0)
			addToResultSet(&candidate, &resultCount);

	} // end while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end))

	if (occ != NULL) {
		for (int i = 0; i < elementCount; i++)
			free(occ[i]);
		free(occ);
		occ = NULL;
		free(areTheSame);
		areTheSame = NULL;
	}

	count = resultCount;
} // end of processCoreQuery()


void BM25Query::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	k1 = getModifierDouble(modifiers, "k1", k1);
	b = getModifierDouble(modifiers, "b", b);
	noIDF = getModifierBool(modifiers, "noidf", false);
	noTF = getModifierBool(modifiers, "notf", false);
	useTermProximity = getModifierBool(modifiers, "tp", false);
	chronologicalTermRank = getModifierDouble(modifiers, "ctr", 0);
} // end of processModifiers(char**)


