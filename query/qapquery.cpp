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
 * Implementation of the generic QAP scoring function.
 *
 * author: Stefan Buettcher
 * created: 2004-10-24
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <math.h>
#include <string.h>
#include "qapquery.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../misc/all.h"


const double QAPQuery::DEFAULT_K1;


void QAPQuery::initialize(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;

	getConfigurationDouble("QAP_K1", &k1, DEFAULT_K1);
	processModifiers(modifiers);
	
	queryString = duplicateString(body);
	actualQuery = this;
	ok = false;
} // end of initialize(Index*, char*, char**, char*, VisibleExtents*, int)


QAPQuery::QAPQuery() {
	mustFreeVisibleExtentsInDestructor = false;
} // end of QAPQuery()


QAPQuery::QAPQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of QAPQuery(...)


QAPQuery::QAPQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of QAPQuery(...)


QAPQuery::~QAPQuery() {
} // end of ~QAPQuery()


/**
 * A QAP query (as any ranked query) has to look like:
 * @rank[...] CONTAINER by ELEM1, ELEM2, ...
 * The CONTAINER part may be omitted, asking to return raw pasages.
 * This method splits the query string into its ingredients.
 **/
bool QAPQuery::parse() {
	if (!parseQueryString(queryString, NULL, NULL, memoryLimit)) {
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


void QAPQuery::processCoreQuery() {
	offset start, end;
	offset nextStart[MAX_SCORER_COUNT], nextEnd[MAX_SCORER_COUNT];
	double maxWithN[MAX_SCORER_COUNT + 2];
	ExtentList *elementList[MAX_SCORER_COUNT];
	for (int i = 0; i < elementCount; i++)
		elementList[i] = elementQueries[i]->getResult();

	ExtentList *containerList = NULL;
	if (containerQuery != NULL)
		containerList = containerQuery->getResult();
	ExtentList *statisticsList = NULL;
	if (statisticsQuery != NULL)
		statisticsList = statisticsQuery->getResult();

	// obtain the total corpus size and per-query-term corpus statistics,
	// as seen by the user associated with this query
	double corpusSize = 0.0;
	offset termCounts[MAX_SCORER_COUNT];
	memset(termCounts, 0, sizeof(termCounts));
	for (int i = 0; i < elementCount; i++)
		if (!elementList[i]->getFirstStartBiggerEq(0, &nextStart[i], &nextEnd[i]))
			nextStart[i] = nextEnd[i] = MAX_OFFSET;
	start = -1;
	if (statisticsList != NULL) {
		// if we have a statistics supplier, use it to get term weights
		while (statisticsList->getFirstStartBiggerEq(start + 1, &start, &end)) {
			corpusSize += (end - start + 1);
			for (int i = 0; i < elementCount; i++)
				if (nextEnd[i] <= end) {
					termCounts[i] += elementList[i]->getCount(start, end);
					if (!elementList[i]->getFirstStartBiggerEq(start + 1, &nextStart[i], &nextEnd[i]))
						nextStart[i] = nextEnd[i] = MAX_OFFSET;
				}
		}
	}
	else {
		// if not, assume the corpus starts at the first occurrence of a query term
		// and ends at the last occurrence of a query term
		offset corpusStart = MAX_OFFSET, corpusEnd = -1;
		for (int i = 0; i < elementCount; i++) {
			if (elementList[i]->getFirstStartBiggerEq(0, &start, &end))
				if (start < corpusStart)
					corpusStart = start;
			if (elementList[i]->getLastEndSmallerEq(MAX_OFFSET, &start, &end))
				if (end > corpusEnd)
					corpusEnd = end;
			termCounts[i] = elementList[i]->getLength();
		}
		corpusSize = (corpusEnd - corpusStart + 1);
		if (corpusSize < 1.0)
			corpusSize = 1.0;
	}
	
	// compute term weights from collection statistics
	for (int i = 0; i < elementCount; i++) {
		if ((termCounts[i] < 1) || (termCounts[i] > corpusSize - 1))
			internalWeights[i] = 0.0;
		else
			internalWeights[i] = externalWeights[i] * log(corpusSize / termCounts[i]);
	}

	// find the term with minimum weight in order to speed up searching
	// (similar to MaxScore heuristic)
	int termWithMinWeight = 0;
	for (int i = 1; i < elementCount; i++)
		if (internalWeights[i] < internalWeights[termWithMinWeight])
			termWithMinWeight = i;

	// sort the term weights in order to compute maximum impact of top-1, top-2,
	// ..., top-N query terms
	for (int i = 0; i < elementCount; i++)
		maxWithN[i] = internalWeights[i];
	qsort(maxWithN, elementCount, sizeof(double), doubleComparator);
	for (int i = 1; i < elementCount; i++)
		maxWithN[i] += maxWithN[i - 1];
	double maxScore = maxWithN[elementCount - 1];

	// initialize heap structure
	results = typed_malloc(ScoredExtent, count + 1);
	int resultCount = 0;

	// Two different cases:
	//  - container query is present => container data have to be put onto heap;
	//  - no container query => passage data have to be put onto heap.
	// We are covering both at the same time.
	bool returnContainer = (containerList != NULL);
	if (containerList == NULL) {
		if (visibleExtents == NULL)
			containerList = new ExtentList_OneElement(0, MAX_OFFSET);
		else
			containerList = visibleExtents->getExtentList();
	}

	// walk through all documents and look for good passages	
	offset contPosition = 0;
	offset contStart, contEnd;
	while (containerList->getFirstEndBiggerEq(contPosition, &contStart, &contEnd)) {
		ScoredExtent candidate;
		contPosition = contEnd + 1;
		candidate.score = -1.0;

		for (int i = 1; i <= elementCount; i++) {

			if (resultCount >= count)
				if (maxWithN[i - 1] <= results[0].score)
					continue;

			// create all i-covers and score them
			offset coverStart = contStart;
			bool atLeastOneHasBeenFound = false;

			while (true) {
				for (int k = 0; k < elementCount; k++) {
					nextEnd[k] = MAX_OFFSET;
					if (elementList[k]->getFirstStartBiggerEq(coverStart, &start, &end))
						if (end <= contEnd)
							nextEnd[k] = end;
				}
				offset coverEnd = quickSelect(nextEnd, i - 1, elementCount);
				if (coverEnd == MAX_OFFSET)
					break;
				else
					atLeastOneHasBeenFound = true;

				offset newCoverStart = MAX_OFFSET;
				double foundCount = 0;
				double score = 0.0;
				for (int k = 0; k < elementCount; k++) {
					if (elementList[k]->getLastEndSmallerEq(coverEnd, &start, &end)) {
						if (start >= coverStart) {
							if (start < newCoverStart)
								newCoverStart = start;
							score += internalWeights[k];
							foundCount += externalWeights[k];
						}
					}
				} // end for (int k = 0; k < elementCount; k++)

				coverStart = newCoverStart;
				score -= foundCount * log(coverEnd - coverStart + 1);

				if (returnContainer) {
					if (score > candidate.score) {
						candidate.from = coverStart;
						candidate.to = coverEnd;
						candidate.score = score;
					}
				}
				else if (score > 0.0) {
					candidate.from = coverStart;
					candidate.to = coverEnd;
					candidate.score = score;
					addToResultSet(&candidate, &resultCount);
				}

				coverStart = coverStart + 1;
			} // end while (true)

			if (!atLeastOneHasBeenFound)
				break;

		} // end for (int i = 1; i <= elementCount; i++)

		if ((returnContainer) && (candidate.score > 0.0)) {
			candidate.containerFrom = contStart;
			candidate.containerTo = contEnd;
			addToResultSet(&candidate, &resultCount);
			if ((resultCount >= count) && (results[0].score >= maxScore))
				break;
		}

		// try to jump over a few documents in order to speed things up...
		if (returnContainer) {
			offset firstPossible = MAX_OFFSET;
			for (int k = 0; k < elementCount; k++) {
				if ((k == termWithMinWeight) && (resultCount >= count))
					continue;
				if (elementList[k]->getFirstStartBiggerEq(contStart + 1, &start, &end))
					if (end < firstPossible)
						firstPossible = end;
			}
			if (firstPossible > contPosition)
				contPosition = firstPossible;
		}
		
	} // end while (containerList->getFirstEndBiggerEq(contPosition, &contStart, &contEnd))

	if (!returnContainer) {
		delete containerList;
		containerList = NULL;
	}

	count = resultCount;
} // end of processCoreQuery()


void QAPQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	k1 = getModifierDouble(modifiers, "k1", k1);
} // end of processModifiers(char**)


offset QAPQuery::quickSelect(offset *array, int rank, int length) {
#if 0
	assert(rank < length);
	if (length == 1)
		return array[0];
	else {
		qsort(array, length, sizeof(offset), offsetComparator);
		return array[rank];
	}
#else
	while (length > 2) {
		int middle = (length >> 1);
		int left = 0;
		int right = length - 1;
		while (right > left) {
			while ((left < middle) && (array[left] <= array[middle]))
				left++;
			while ((right > middle) && (array[right] >= array[middle]))
				right--;
			if (right > left) {
				offset temp = array[left];
				array[left] = array[right];
				array[right] = temp;
				if (middle == left) {
					middle = right;
					left++;
				}
				else if (middle == right) {
					middle = left;
					right--;
				}
			}
		}
		if (rank < middle)
			length = middle;
		else if (rank > middle) {
			length -= (middle + 1);
			rank -= (middle + 1);
			array = &array[middle + 1];
		}
		else
			return array[middle];
	}
	if (length == 1)
		return array[0];
	else if (array[0] <= array[1])
		return array[rank];
	else
		return array[1 - rank];
#endif
} // end of quickSelect(offset*, int)


bool QAPQuery::getNextLine(char *line) {
	if (!ok) {
		finished = true;
		return false;
	}
	if (position >= count) {
		finished = true;
		return false;
	}
	if (results[position].score <= 0.0) {
		finished = true;
		return false;
	}
	if (containerQuery == NULL)
		sprintf(line, "%s %f %lld %lld",
				queryID, results[position].score,
				(long long)results[position].from, (long long)results[position].to);
	else
		sprintf(line, "%s %f %lld %lld %lld %lld",
				queryID, results[position].score,
				(long long)results[position].containerFrom, (long long)results[position].containerTo,
				(long long)results[position].from, (long long)results[position].to);

	if (additionalQuery != NULL)
		addAdditionalStuffToResultLine(line, results[position].from, results[position].to);
	if (getAnnotation)
		addAnnotationToResultLine(line, results[position].from);
	if (printFileName)
		addFileNameToResultLine(line, results[position].from);
	if (printPageNumber)
		addPageNumberToResultLine(line, results[position].from, results[position].to);
	if (printDocumentID) {
		char docID[256];
		getDocIdForOffset(docID, results[position].from, results[position].to, false);
		sprintf(&line[strlen(line)], " \"%s\"", docID);
	}

	position++;
	return true;
} // end of getNextLine(char*)



