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
 * created: 2007-03-14
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "vectorspace_query.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"


static const char *LOG_ID = "VectorSpaceQuery";


void VectorSpaceQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	actualQuery = this;
	docLens = NULL;
	docCnt = -1;
	fd = -1;
	ok = false;
} // end of initialize(...)


VectorSpaceQuery::VectorSpaceQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of VectorSpaceQuery(...)


VectorSpaceQuery::VectorSpaceQuery(
		Index *index, const char *command, const char **modifiers, const char *body, uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of VectorSpaceQuery(...)


VectorSpaceQuery::~VectorSpaceQuery() {
	if (docLens != NULL) {
		munmap(docLens, docCnt * sizeof(VectorSpaceDocLen));
		docLens = NULL;
	}
	if (fd >= 0)
		close(fd);
} // end of ~VectorSpaceQuery()


double VectorSpaceQuery::getVectorLength(offset documentStart) {
	if (docCnt < 0) {
		char *fileName;
		if (useIDF)
			fileName = evaluateRelativePathName(index->directory, "doclens.tfidf");
		else
			fileName = evaluateRelativePathName(index->directory, "doclens.tf");
		fd = open(fileName, O_RDONLY);
		if (fd >= 0) {
			struct stat buf;
			stat(fileName, &buf);
			assert(buf.st_size > 0);
			docCnt = buf.st_size / sizeof(VectorSpaceDocLen);
			docLens = (VectorSpaceDocLen*)mmap(
					NULL, docCnt * sizeof(VectorSpaceDocLen), PROT_READ, MAP_PRIVATE, fd, 0);
		}
		else {
			snprintf(errorMessage, sizeof(errorMessage),
				"Unable to open file with vector length information: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			log(LOG_ERROR, LOG_ID, "Assuming unit length for every document.");
			docCnt = 0;
		}
		free(fileName);
	}
	if (docCnt == 0)
		return 1;
	
	int lower = 0;
	int upper = docCnt - 1;
	while (lower < upper) {
		int middle = (lower + upper) >> 1;
		if (docLens[middle].docStart < documentStart)
			lower = middle + 1;
		else
			upper = middle;
	}
	if (docLens[lower].docStart == documentStart)
		return docLens[lower].docLen;
	else {
		log(LOG_ERROR, LOG_ID, "Data in doclens.* file do not match index data. Assuming unit length for every document.");
		munmap(docLens, docCnt * sizeof(VectorSpaceDocLen));
		docLens = NULL;
		docCnt = 0;
		return 1;
	}
} // end of getVectorLength(offset)


void VectorSpaceQuery::processCoreQuery() {
	offset start, end;
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();
	ExtentList *statisticsList = statisticsQuery->getResult();

	double documentCount = containerList->getLength();
	if (documentCount < 1) {
		// no matching container found: stop execution
		count = 0;
		return;
	}

	// compute IDF values
	for (int i = 0; i < elementCount; i++) {
		elementLists[i] = elementQueries[i]->getResult();
		ExtentList_Containment list(
				new ExtentList_Copy(statisticsList), new ExtentList_Copy(elementLists[i]), true, false);
		double df = MIN(documentCount, MAX(0.5, list.getLength()));
		internalWeights[i] = MAX(0, externalWeights[i] * log(documentCount / df));
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

		// compute document score according to vector space model: (log tf) * (log idf)
		// current document's language model
		for (int i = 0; i < elementCount; i++) {
			double tf = elementLists[i]->getCount(start, end);
			double termWeight = internalWeights[i];
			if (useIDF) {
				// add IDF factor for document vector in addition to that from the query vector
				termWeight *= internalWeights[i] / externalWeights[i];
			}
			if (tf > 0)
				candidate.score += termWeight * (linearTF ? tf : log(tf) / log(2) + 1);
		}

		if (candidate.score > 0) {
			// apply document length normalization
			if (!rawScores) {
				double vl = getVectorLength(start);
				if (vl < 0)
					break;
				candidate.score /= vl;
			}

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


void VectorSpaceQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	useIDF = !getModifierBool(modifiers, "noidf", false);
	rawScores = getModifierBool(modifiers, "raw", false);
	linearTF = getModifierBool(modifiers, "linear_tf", false);
} // end of processModifiers(char**)


