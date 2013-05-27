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
 * Implementation of the @tier query: Cover Density Ranking with Term Coordination Levels.
 *
 * author: Stefan Buettcher
 * created: 2005-10-12
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include "tierquery.h"
#include "getquery.h"
#include "querytokenizer.h"
#include "../misc/all.h"


const int TierQuery::PASSAGE_LENGTH;


TierQuery::TierQuery() {
	mustFreeVisibleExtentsInDestructor = false;
} // end of TierQuery()


TierQuery::TierQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of TierQuery(...)


TierQuery::TierQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = true;
} // end of TierQuery(...)


TierQuery::~TierQuery() {
} // end of ~TierQuery()


bool TierQuery::parse() {
	// if no container is given, assume default container
	char defaultContainer[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue("DEFAULT_RETRIEVAL_SET", defaultContainer))
		strcpy(defaultContainer, DOC_QUERY);

	if (!parseQueryString(queryString, defaultContainer, NULL, memoryLimit)) {
		syntaxErrorDetected = finished = true;
		ok = false;
	}
	else if (elementCount > 8) {
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


static int compareScoredQueries(const void *a, const void *b) {
	ScoredQuery *x = (ScoredQuery*)a;
	ScoredQuery *y = (ScoredQuery*)b;
	if (x->score > y->score)
		return -1;
	else if (x->score < y->score)
		return +1;
	else
		return 0;
} // end of compareScoredQueries(const void*, const void*)


void TierQuery::getSubQuery(int n, ScoredQuery *q) {
	// create AND query from all "1" bits in the "n" vector; adjust query score
	// to reflect the QAP score of a passage of length PASSAGE_LENGTH containing
	// the given query terms
	int hw = getHammingWeight(n);
	double score = -hw * log(PASSAGE_LENGTH) / LOG_2;
	ExtentList **crap = typed_malloc(ExtentList*, hw + 1);
	for (int i = 0, k = 1; i < elementCount; i++, k <<= 1)
		if (n & k) {
			crap[--hw] = new ExtentList_Copy(elementQueries[i]->getResult());
			score += internalWeights[i];
		}
	assert(hw == 0);
	q->list = new ExtentList_AND(crap, getHammingWeight(n));
	q->score = score;
} // end of getSubQuery(int, ScoredQuery*)


void TierQuery::processCoreQuery() {
	// compute QAP-style term weights
	computeTermCorpusWeights();

	static const int MAX_SUBQUERY_COUNT = 256;
	int subQueryCount = 0;
	ScoredQuery subQueries[MAX_SUBQUERY_COUNT];

	// create the 2^elementCount sub-queries
	for (int hw = elementCount; (hw >= elementCount - 3) && (hw > 0); hw--) {
		for (int i = 1; i < pow(2.0, elementCount) - 0.1; i++)
			if (getHammingWeight(i) == hw) {
				getSubQuery(i, &subQueries[subQueryCount]);
				subQueryCount++;
				if (subQueryCount >= MAX_SUBQUERY_COUNT)
					break;
			}
		if (subQueryCount >= MAX_SUBQUERY_COUNT)
			break;
	} // end for (int hw = elementCount; (hw >= elementCount - 3) && (hw > 0); hw--)

	// sort queries by their QAP score
	qsort(subQueries, subQueryCount, sizeof(ScoredQuery), compareScoredQueries);

	// initialize result list
	ScoredExtent *sexes = typed_malloc(ScoredExtent, (count + 1));
	int maxCount = count;
	int sexCount = 0;

	// execute CDRQuery::execute for all sub-queries
	for (int i = 0; (i < subQueryCount) && (sexCount < maxCount); i++) {

		// group subqueries such that the subquery with the least score in each
		// group has at least 50% of the score of the highest-scoring subquery
		int j = i;
		while (j < subQueryCount) {
			if (subQueries[j].score < subQueries[i].score - 1.0)
				break;
			j++;
		}
		if (i == j - 1)
			scorer = subQueries[i].list;
		else {
			ExtentList **scorers = typed_malloc(ExtentList*, j - i);
			for (int k = 0; k < j - i; k++)
				scorers[k] = subQueries[i + k].list;
			scorer = new ExtentList_OR(scorers, j - i);
		}

		// execute CDR query for current scorer
		count = maxCount - sexCount;
		CDRQuery::processCoreQuery();
		delete scorer;
		i = j - 1;

		// copy results from CDR query to real result list
		int pos = 0;
		while ((sexCount < maxCount) && (pos < count)) {
			// make sure the given document does not appear in the final result list yet
			bool found = false;
			for (int k = 0; k < sexCount; k++)
				if (sexes[k].containerFrom == results[pos].containerFrom)
					found = true;
			if (found) {
				pos++;
				continue;
			}
			results[pos].score = subQueryCount - i + 1.0 / (pos + 2);
			sexes[sexCount++] = results[pos++];
		} // end while ((sexCount < maxCount) && (pos < count))

		free(results);
	} // end for (int i = 0; (i < subQueryCount) && (sexCount < maxCount); i++)

	assert(sexCount <= maxCount);

	// perform final sorting of the result list, this time by descending score
	sortResultsByScore(sexes, sexCount, false);
	count = sexCount;
	results = sexes;
} // end of processCoreQuery()

