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
 * Implementation of the @qap2 query class.
 *
 * author: Stefan Buettcher
 * created: 2005-10-14
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <math.h>
#include <string.h>
#include "qap2query.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../misc/all.h"



QAP2Query::QAP2Query() {
	mustFreeVisibleExtentsInDestructor = false;
} // end of QAP2Query()


QAP2Query::QAP2Query(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of QAP2Query(...)


QAP2Query::QAP2Query(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of QAP2Query(...)


QAP2Query::~QAP2Query() {
} // end of ~QAP2Query()


int compareOccurrences(const void *a, const void *b) {
	Occurrence *x = (Occurrence*)a;
	Occurrence *y = (Occurrence*)b;
	if (x->start < y->start)
		return -1;
	else if (x->start > y->start)
		return +1;
	else
		return (int)(x->end - y->end);
} // end of compareOccurrences(const void*, const void*)


ScoredExtent * QAP2Query::getPassages(Occurrence *occ, int count, double avgdl) {
	if (count <= 0) {
		ScoredExtent *result = typed_malloc(ScoredExtent, 1);
		result[0].score = -1.0;
		return result;
	} // end if (count <= 0)
	else if (count == 1) {
		double dl = occ[0].end - occ[0].start + 1;
		double K = k1 * (1 - b + b * dl / avgdl);
		ScoredExtent *result = typed_malloc(ScoredExtent, 2);
		result[0].from = occ[0].start;
		result[0].to = occ[0].end;
		result[0].score = internalWeights[occ[0].who] * (k1 * 1) / (K + 1);
		result[1].score = -1.0;
		return result;
	} // end else if (count == 1)
	else {
		double bestScore = -1.0;
		int bestStart = -1, bestEnd = -1;
		int tf[MAX_SCORER_COUNT];
		for (int start = 0; start < count; start++) {
			offset min = MAX_OFFSET, max = 0;
			memset(tf, 0, elementCount * sizeof(int));
			min = occ[start].start;
			max = occ[start].end;
			for (int end = start; end < count; end++) {
				if (occ[end].start < min)
					min = occ[end].start;
				if (occ[end].end > max)
					max = occ[end].end;
				tf[occ[end].who]++;

				double dl = (max - min + 1);
				assert(dl > 0.0);
				double K = k1 * (1 - b + b * dl / avgdl);
				double score = 0.0;
				for (int i = 0; i < elementCount; i++)
					score += internalWeights[i] * (k1 * tf[i]) / (K + tf[i]);
				if (score > bestScore) {
					bestStart = start;
					bestEnd = end;
					bestScore = score;
				}
			}
		} // end for (int start = 0; start < count; start++)
//		ScoredExtent *left = getPassages(occ, bestStart, avgdl);
//		ScoredExtent *right = getPassages(&occ[bestEnd + 1], count - bestEnd - 1, avgdl);
		ScoredExtent *left = getPassages(occ, 0, avgdl);
		ScoredExtent *right = getPassages(&occ[bestEnd + 1], 0, avgdl);

		// count extents in sub-lists from left and right
		int extentCount = 0;
		for (int i = 0; left[i].score > 0.0; i++)
			extentCount++;
		for (int i = 0; right[i].score > 0.0; i++)
			extentCount++;

		// merge sub-lists into one big list
		ScoredExtent *result = typed_malloc(ScoredExtent, extentCount + 2);
		result[0].from = occ[bestStart].start;
		result[0].to = occ[bestEnd].end;
		result[0].score = bestScore;
		extentCount = 1;
		for (int i = 0; left[i].score > 0.0; i++)
			result[extentCount++] = left[i];
		for (int i = 0; right[i].score > 0.0; i++)
			result[extentCount++] = right[i];
		result[extentCount].score = -1.0;

		// free sub-lists and return merged list
		free(left); free(right);
		return result;
	} // end else [count > 1]
} // end of getPassages(Occurrence*, int, double)


static const int PREVIEW = 64;


void QAP2Query::processCoreQuery() {
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

	// alright, we have found the average document length; the next thing we do is
	// we compute the average length of all minimal passages containing all query
	// terms; if it is smaller than the average document length, we take this value
	// as avgdl instead of the old one
	ExtentList **copies = typed_malloc(ExtentList*, elementCount);
	for (int i = 0; i < elementCount; i++)
		copies[i] = new ExtentList_Copy(elementLists[i]);
	ExtentList *AND = new ExtentList_AND(copies, elementCount);
	ExtentList *containerThing =
		new ExtentList_Containment(new ExtentList_Copy(containerList),
				new ExtentList_Copy(AND), true, false);
	ExtentList *containeeThing =
		new ExtentList_Containment(new ExtentList_Copy(containerList),
				new ExtentList_Copy(AND), false, false);

	double avgContainerThing = containerThing->getTotalSize() / (1.0 * containerThing->getLength());
	double avgContaineeThing = containeeThing->getTotalSize() / (1.0 * containeeThing->getLength());

	if (containeeThing->getLength() > 100) {
		double ratio = avgContaineeThing / avgContainerThing;
		assert(ratio <= 1.0);
		averageContainerLength = (averageContainerLength + ratio * averageContainerLength) / 2;
	}

	delete containerThing;
	delete containeeThing;
	delete AND;

	// compute the BM25 term weight for all elements
	for (int i = 0; i < elementCount; i++) {
		if ((positiveContainerCount[i] < 1) || (positiveContainerCount[i] > containerCount - 1))
			internalWeights[i] = 0;
		else
			internalWeights[i] =
				externalWeights[i] * log((1.0 * containerCount) / positiveContainerCount[i]);
	}

	// initialize heap structure
	ScoredExtent sex;
	ScoredExtent *sexes = typed_malloc(ScoredExtent, count + 1);
	int sexCount = 0;

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

	int tf[MAX_SCORER_COUNT], whichScorer[MAX_SCORER_COUNT];

	while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end)) {
		sex.from = start;
		sex.to = end;
		sex.score = 0.0;

		double containerLength = (end - start + 1);
		double K = k1 * ((1 - b) + b * containerLength / averageContainerLength);
		int scorersInCurrentDocument = 0;
		nextOffsetPossible = MAX_OFFSET;

		Occurrence *occ = typed_malloc(Occurrence, 1);
		int occCount = 0;

		// compute score for every term in the element list
		for (int i = 0; i < elementCount; i++) {
			ExtentList *elemList = elementLists[i];
			if (nextPossibleForElement[i] > end) {
				// this element cannot be found inside the current document
				if (nextPossibleForElement[i] < nextOffsetPossible)
					nextOffsetPossible = nextPossibleForElement[i];
			}
			else {
				// could be that we find the element inside the current document
				offset position = start;
				if (elemList->getFirstStartBiggerEq(position, &elemStart, &elemEnd)) {
					nextPossibleForElement[i] = elemEnd;
					if (nextPossibleForElement[i] < nextOffsetPossible)
						nextOffsetPossible = nextPossibleForElement[i];
					offset count = elemList->getCount(start, end);
					if (count > 0) {
						offset *s = typed_malloc(offset, count);
						offset *e = typed_malloc(offset, count);
						int cnt = elemList->getNextN(start, end, count, s, e);
						assert(cnt == count);
						occ = typed_realloc(Occurrence, occ, occCount + count);
						for (int k = 0; k < count; k++) {
							assert(e[k] >= s[k]);
							occ[occCount + k].start = s[k];
							occ[occCount + k].end = e[k];
							occ[occCount + k].who = i;
						}
						free(s); free(e);
						occCount += count;
					}
				}
				else
					nextPossibleForElement[i] = MAX_OFFSET;
			}
		} // end for (int i = 0; i < elementCount; i++)

		if (occCount > 0) {
			if (occCount > 1)
				qsort(occ, occCount, sizeof(Occurrence), compareOccurrences);
			sex.containerFrom = start;
			sex.containerTo = end;
			sex.score = 0.0;
			ScoredExtent *passages = getPassages(occ, occCount, averageContainerLength);
			for (int i = 0; passages[i].score > 0; i++)
				sex.score += passages[i].score * pow(0.5, i);
			free(passages);
		}
		else
			sex.score = 0.0;
		free(occ);

		if (nextOffsetPossible <= end)
			nextOffsetPossible = end + 1;

		if (sex.score > 0.0) {
		 	// we have a heap structure that contains the best "count" containers
			if (sexCount < count) {
				sexes[sexCount++] = sex;
				if (sexCount >= count)
					sortResultsByScore(sexes, sexCount, true);
			}
			else if (sex.score > sexes[0].score) {
				// sexes[0] contains the extent with minimum score; remove it from the
				// heap and adjust the position for the new element
				sexes[0] = sex;
				moveFirstHeapNodeDown(sexes, sexCount);
			}
		} // end if (sex.score > 0.0)

	} // end while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end))

	sortResultsByScore(sexes, sexCount, false);
	count = sexCount;
	results = sexes;
} // end of processCoreQuery()


void QAP2Query::printResultLine(char *target, ScoredExtent sex) {
	sex.from = sex.to = 0;
	sprintf(target, "%s %f " OFFSET_FORMAT " " OFFSET_FORMAT " " OFFSET_FORMAT " " OFFSET_FORMAT,
			queryID, sex.score, sex.containerFrom, sex.containerTo, sex.from, sex.to);
} // end of printResultLine(char*, ScoredExtent)


