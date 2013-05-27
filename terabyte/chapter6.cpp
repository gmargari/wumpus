/**
 * Copyright (C) 2008 Stefan Buettcher. All rights reserved.
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
 * created: 2008-08-14
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include <map>
#include <vector>
#include "chapter6.h"
#include "../indexcache/docidcache.h"
#include "../misc/all.h"


using namespace std;


static const char * LOG_ID = "Chapter6";


// Whether the TF values in the index are actually TF values or final BM25
// score contribs.
#define INDEX_CONTAINS_PRECOMPUTED_SCORES 0
#define BITS_PER_SCORE_CONTRIB 5
#define SHIFT_FOR_SCORE_CONTRIB (BITS_PER_SCORE_CONTRIB < 5 ? 5 : BITS_PER_SCORE_CONTRIB)
	// We always allocate at least 5 bits for the score component of each
	// posting, even if we only use 2 or 3.


class DocLenCache {
public:
	static void getDocLens(ExtentList *documents, float **doclens, float *avgdl) {
		LocalLock lock(&lockable);
		if (DocLenCache::doclens == NULL) {
			int documentCount = documents->getLength();
			DocLenCache::doclens = typed_malloc(float, documentCount);
			double totalLen = 0.0;
			int outPos = 0;
			offset s = -1, e;
			while (documents->getFirstStartBiggerEq(s + 1, &s, &e)) {
				int len = (e - s - 1);
				DocLenCache::doclens[outPos++] = len;
				totalLen += len;
			}
			assert(outPos == documentCount);
			DocLenCache::avgdl = totalLen / documentCount;
		}
		*doclens = DocLenCache::doclens;
		*avgdl = DocLenCache::avgdl;
	}

private:
	static Lockable lockable;
	static float *doclens;
	static float avgdl;
};

float * DocLenCache::doclens = NULL;
float DocLenCache::avgdl;
Lockable DocLenCache::lockable;


static inline int getDocIdFromPosting(offset posting) {
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
	return (posting >> SHIFT_FOR_SCORE_CONTRIB);
#else
	return (posting >> DOC_LEVEL_SHIFT);
#endif
}


void Chapter6::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	bool positionless;
	getConfigurationBool("POSITIONLESS_INDEXING", &positionless, false);
	assert(positionless);

	this->userID = Index::GOD;
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->memoryLimit = memoryLimit;
	mustFreeVisibleExtentsInDestructor = false;
	processModifiers(modifiers);
	queryString = duplicateString(body);
	actualQuery = this;
	ok = false;
} // end of initialize(...)


Chapter6::Chapter6(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
} // end of Chapter6(...)


Chapter6::Chapter6(Index *index, const char *command, const char **modifiers,
		const char *body, uid_t userID, int memoryLimit) {
	initialize(index, command, modifiers, body, NULL, memoryLimit);
} // end of Chapter6(...)


Chapter6::~Chapter6() {
} // end of ~Chapter6()


GCLQuery * Chapter6::createElementQuery(const char *query, double *weight, int memoryLimit) {
	GCLQuery *result = RankedQuery::createElementQuery(query, weight, -1);
	assert(result != NULL);

	// Obtain the query string for this scorer and remove the surrounding
	// quotation marks.
	char *queryString = result->getQueryString();
	trimString(queryString);
	int len = strlen(queryString);
	assert(queryString[0] == '"' && queryString[len - 1] == '"');
	memmove(queryString, queryString + 1, len - 2);
	queryString[len - 2] = 0;
	delete result;

	// Construct a new query term that gets us document-level postings.
	char *newQueryString = concatenateStrings("<!>", queryString);
	free(queryString);
	ExtentList *postingList = index->getPostings(newQueryString, Index::GOD);
	free(newQueryString);
	return new GCLQuery(index, postingList);
}	


/**
 * How many postings do we retrieve from a PostingList in a single call? This
 * is used to increase QP performance by reducing the number of virtual method
 * calls.
 **/
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


void Chapter6::processCoreQuery() {
	if (ntoulas)
		executeQuery_Ntoulas();
	else if (conjunctive)
		executeQuery_Conjunctive();
	else if (termAtATime)
		executeQuery_TermAtATime();
	else
		executeQuery_DocumentAtATime();
} // end of processCoreQuery()


void Chapter6::computeTermWeights(ExtentList **elementLists, double containerCount) {
	// compute the term weight for all elements
	for (int i = 0; i < elementCount; i++) {
		offset lastStart, lastEnd;
		offset listLength = elementLists[i]->getLength();
		if (listLength == 0) {
			internalWeights[i] = log(containerCount + 1);
			continue;
		}
		// for impact-ordered (and -restricted) lists, we encode the length of the
		// original posting list in the last element of the new list, as a delta
		// relative to DOCUMENT_COUNT_OFFSET
		if (elementLists[i]->getFirstStartBiggerEq(DOCUMENT_COUNT_OFFSET, &lastStart, &lastEnd)) {
			listLength = lastStart - DOCUMENT_COUNT_OFFSET;
			assert(listLength > 0);
		}
		double df = listLength;
		if ((df < 1) || (df > containerCount - 1))
			internalWeights[i] = 0;
		else
			internalWeights[i] = externalWeights[i] * log(containerCount / df);
	} // end for (int i = 0; i < elementCount; i++)
}


void Chapter6::executeQuery_Ntoulas() {
	ExtentList* containerList = containerQuery->getResult();
	double containerCount = containerList->getLength();
	ExtentList *elementLists[MAX_SCORER_COUNT];

	for (int i = 0; i < elementCount; i++)
		elementLists[i] = elementQueries[i]->getResult();
	computeTermWeights(elementLists, containerCount);

	float* doclens;
	float avgdl;
 	DocLenCache::getDocLens(containerList, &doclens, &avgdl);

	float minContrib[MAX_SCORER_COUNT];
	for (int i = 0; i < elementCount; i++) {
		minContrib[i] = internalWeights[i] * (k1 + 1.0f);
		offset s = -1, e;
		while (elementLists[i]->getFirstStartBiggerEq(s + 1, &s, &e)) {
			int docid = getDocIdFromPosting(s);
			if (docid > containerCount - 0.5)
				break;
			float dl = doclens[docid];
			float tf = decodeDocLevelTF(s & DOC_LEVEL_MAX_TF);
			float K = k1 * (1 - b + b * dl / avgdl);
			float score = internalWeights[i] * (tf * (k1 + 1.0f)) / (tf + K);
			if (score < minContrib[i])
				minContrib[i] = score;
		}
	}

	// initialize heap structure for result extents
	ScoredExtent sex;
	ScoredExtent *sexes = typed_malloc(ScoredExtent, 2 * count + 2);
	int sexCount = 0;
	offset dummy[PREVIEW + 2];

	// initialize heap structure for scorers; add sentinels at the end of the
	// heap; this will save us a couple of "if" statements
	LHS **heap = typed_malloc(LHS*, elementCount * 2 + 2);
	for (int elem = 0; elem < elementCount * 2 + 2; elem++) {
		heap[elem] = typed_malloc(LHS, 1);
		heap[elem]->who = elem;
		heap[elem]->previewPos = PREVIEW;
		heap[elem]->previewCount = PREVIEW;
		if (elem < elementCount) {
			offset dummy;
			if (!elementLists[elem]->getFirstStartBiggerEq(0, &heap[elem]->next, &dummy))
				heap[elem]->next = MAX_OFFSET;
		}
		else
			heap[elem]->next = MAX_OFFSET;
	}
	qsort(heap, elementCount, sizeof(LHS*), lhsComparator);

	int tf[MAX_SCORER_COUNT];
	int whichScorer[MAX_SCORER_COUNT];

	// this is the lowest score on the heap
	float worstScore = 0.0;

	while (heap[0]->next < MAX_OFFSET) {
		offset where = (heap[0]->next | DOC_LEVEL_MAX_TF);
		if (where >= DOCUMENT_COUNT_OFFSET)
			break;
		float dl = doclens[getDocIdFromPosting(where)];
		float K = k1 * (1 - b + b * dl / avgdl);

		float score = 0.0;

		// loop over all query terms appearing in the current document and compute
		// the document's score
		int termsInCurrentDocument = 0;
		do {
			int who = heap[0]->who;
			int encodedTermFrequency = (int)(heap[0]->next & DOC_LEVEL_MAX_TF);
			float tf = decodeDocLevelTF(encodedTermFrequency);
			score += internalWeights[who] * (tf * (k1 + 1.0f)) / (tf + K);
			termsInCurrentDocument |= (1 << who);

			// remove head of queue for current top element; load new data from
			// list if necessary
			if (heap[0]->previewPos < heap[0]->previewCount)
				heap[0]->next = heap[0]->preview[heap[0]->previewPos++];
			else {
				if (heap[0]->previewCount >= PREVIEW) {
					heap[0]->previewPos = 0;
					heap[0]->previewCount =
						elementLists[who]->getNextN(where + 1, MAX_OFFSET, PREVIEW, heap[0]->preview, dummy);
					if (heap[0]->previewCount > 0)
						heap[0]->next = heap[0]->preview[heap[0]->previewPos++];
					else
						heap[0]->next = MAX_OFFSET;
				}
				else
					heap[0]->next = MAX_OFFSET;
			}

			// perform a reheap operation on the scorer heap
			if (elementCount <= 3) {
				LHS *orig = heap[0];
				if (heap[1]->next < orig->next) {
					heap[0] = heap[1];
					if (heap[2]->next < orig->next) {
						heap[1] = heap[2];
						heap[2] = orig;
					}
					else
						heap[1] = orig;
				}
			}
			else {
				LHS *orig = heap[0];
				int node = 0, leftChild = 1, rightChild = 2;
				while (true) {
					int child = leftChild;
					if (heap[rightChild]->next < heap[leftChild]->next)
						child = rightChild;
					if (orig->next <= (heap[child]->next | DOC_LEVEL_MAX_TF))
						break;
					heap[node] = heap[child];
					node = child;
					leftChild = node + node + 1;
					rightChild = node + node + 2;
				}
				heap[node] = orig;
			}
		} while (heap[0]->next <= where);

		for (int i = 0; i < elementCount; i++)
			if ((termsInCurrentDocument & (1 << i)) == 0)
				score += minContrib[i];

	 	// we have a heap structure that contains the best "count" containers; only add
		// the current candidate to the heap if it can beat the worst extent on the heap
		if (score > worstScore) {
			sex.score = score;
			sex.from = getDocIdFromPosting(where);
			sex.to = sex.from;
			sex.additional = termsInCurrentDocument;
			if (sexCount < count) {
				sexes[sexCount++] = sex;
				if (sexCount >= count) {
					sortResultsByScore(sexes, sexCount, true);
					for (int i = count; i < count * 2 + 2; i++)
						sexes[i].score = 999999.999;  // sentinels
					worstScore = sexes[0].score;
				}
			}
			else {
				// insert the new ScoredExtent instance into the heap; perform a reheap operation
				// in order to have the lowest-scoring candidate on the top of the heap again
				int node = 0, child = 1;
				while (true) {
					if (sexes[child + 1].score < sexes[child].score)
						child = child + 1;
					if (sexes[child].score >= sex.score)
						break;
					sexes[node] = sexes[child];
					node = child;
					child = node + node + 1;
				}
				sexes[node] = sex;
				worstScore = sexes[0].score;
			}
		}

	} // end while (heap[0]->next < MAX_OFFSET)

	for (int elem = 0; elem < elementCount * 2 + 2; elem++)
		free(heap[elem]);
	free(heap);

	if (sexCount < count)
		count = sexCount;
	results = sexes;
	sortResultsByScore(results, count, false);

	// Compute the indicator as 1 if all of the top k results contain all
	// query terms; 0 otherwise.
	int correctnessIndicator = 1;
	for (int i = 0; i < count; i++)
		if (results[i].additional != (1 << elementCount) - 1)
			correctnessIndicator = 0;
	printf("C = %d\n", correctnessIndicator);
}


void Chapter6::executeQuery_Conjunctive() {
	if (count <= 0) {
		results = typed_malloc(ScoredExtent, 1);
		count = 0;
		return;
	}

	ExtentList* containerList = containerQuery->getResult();
	double containerCount = containerList->getLength();
	ExtentList *elementLists[MAX_SCORER_COUNT];

	for (int i = 0; i < elementCount; i++)
		elementLists[i] = elementQueries[i]->getResult();
	computeTermWeights(elementLists, containerCount);

	float* doclens;
	float avgdl;
 	DocLenCache::getDocLens(containerList, &doclens, &avgdl);

	// initialize heap structure for result extents
	ScoredExtent sex;
	ScoredExtent *sexes = typed_malloc(ScoredExtent, 2 * count + 2);
	int sexCount = 0;
	offset dummy[PREVIEW + 2];

	// sort scorers by their lengths and store the new ordering in whichScorer
	int whichScorer[MAX_SCORER_COUNT];
	for (int i = 0; i < elementCount; i++)
		whichScorer[i] = i;
	for (int i = 0; i < elementCount; i++) {
		int best = i;
		for (int k = i + 1; k < elementCount; k++)
			if (elementLists[whichScorer[k]]->getLength() < elementLists[whichScorer[best]]->getLength())
				best = k;
		int tmp = whichScorer[i];
		whichScorer[i] = whichScorer[best];
		whichScorer[best] = tmp;
	}
	for (int i = 0; i < elementCount - 1; i++)
		assert(elementLists[whichScorer[i]]->getLength() <= elementLists[whichScorer[i + 1]]->getLength());
	for (int i = 0; i < elementCount; i++)
		elementLists[i] = elementQueries[whichScorer[i]]->getResult();

	// this is the lowest score on the heap
	float worstScore = 0.0;

	int tf[MAX_SCORER_COUNT];
	offset where = 0, s, e;
	while (elementLists[0]->getFirstStartBiggerEq(where, &s, &e)) {
		static const offset MASK = ~(DOC_LEVEL_MAX_TF);
		tf[0] = (s & DOC_LEVEL_MAX_TF);
		where = (s & MASK);

		bool allFound = true;
		for (int i = 1; i < elementCount; i++) {
			if (!elementLists[i]->getFirstStartBiggerEq(where, &s, &e)) {
				where = MAX_OFFSET;
				allFound = false;
				break;
			}
			if (s > where + DOC_LEVEL_MAX_TF) {
				where = (s & MASK);
				allFound = false;
				break;
			}
			tf[i] = (s & DOC_LEVEL_MAX_TF);
		}
		if (!allFound)
			continue;

		if (where >= DOCUMENT_COUNT_OFFSET)
			break;

		float score = 0.0;
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
		for (int i = 0; i < elementCount; i++) {
			int who = whichScorer[i];
			float scoreContrib = (tf[i] + 0.5f) * (2.2f / (1 << BITS_PER_SCORE_CONTRIB));
			score += internalWeights[who] * scoreContrib;
		}
#else
		float dl = doclens[getDocIdFromPosting(where)];
		float K = k1 * (1 - b + b * dl / avgdl);
		for (int i = 0; i < elementCount; i++) {
			int who = whichScorer[i];
			float decodedTF = decodeDocLevelTF(tf[i]);
			score += internalWeights[who] * (decodedTF * (k1 + 1.0f)) / (decodedTF + K);
		}
#endif

	 	// we have a heap structure that contains the best "count" containers; only add
		// the current candidate to the heap if it beats the worst extent on the heap
		if (score > worstScore) {
			sex.score = score;
			sex.from = getDocIdFromPosting(where);
			sex.to = sex.from;
			if (sexCount < count) {
				sexes[sexCount++] = sex;
				if (sexCount >= count) {
					sortResultsByScore(sexes, sexCount, true);
					for (int i = count; i < count * 2 + 2; i++)
						sexes[i].score = 999999.999;  // sentinels
					worstScore = sexes[0].score;
				}
			}
			else {
				// insert the new ScoredExtent instance into the heap; perform a reheap operation
				// in order to have the lowest-scoring candidate on the top of the heap again
				int node = 0, child = 1;
				while (true) {
					if (sexes[child + 1].score < sexes[child].score)
						child = child + 1;
					if (sexes[child].score >= sex.score)
						break;
					sexes[node] = sexes[child];
					node = child;
					child = node + node + 1;
				}
				sexes[node] = sex;
				worstScore = sexes[0].score;
			}
		}

		where += (DOC_LEVEL_MAX_TF + 1);
	} // end while (heap[0]->next < MAX_OFFSET)

	if (sexCount < count)
		count = sexCount;
	results = sexes;
	sortResultsByScore(results, count, false);

	// Since we use positionless indexing, the "from" component of each result extent
	// only contains a document number, not an actual offset. We need to translate
	// that into start/end offsets for the respective document.
	for (int i = 0; i < count; i++)
		containerList->getNth(results[i].from, &results[i].from, &results[i].to);
}


void Chapter6::executeQuery_DocumentAtATime() {
	if (count <= 0) {
		results = typed_malloc(ScoredExtent, 1);
		count = 0;
		return;
	}

	ExtentList* containerList = containerQuery->getResult();
	double containerCount = containerList->getLength();
	ExtentList *elementLists[MAX_SCORER_COUNT];

	for (int i = 0; i < elementCount; i++)
		elementLists[i] = elementQueries[i]->getResult();
	computeTermWeights(elementLists, containerCount);

	float* doclens;
	float avgdl;
 	DocLenCache::getDocLens(containerList, &doclens, &avgdl);

	// initialize heap structure for result extents
	ScoredExtent sex;
	ScoredExtent *sexes = typed_malloc(ScoredExtent, 2 * count + 2);
	int sexCount = 0;
	offset dummy[PREVIEW + 2];

	// initialize heap structure for scorers; add sentinels at the end of the
	// heap; this will save us a couple of "if" statements
	LHS **heap = typed_malloc(LHS*, elementCount * 2 + 2);
	for (int elem = 0; elem < elementCount * 2 + 2; elem++) {
		heap[elem] = typed_malloc(LHS, 1);
		heap[elem]->who = elem;
		heap[elem]->previewPos = PREVIEW;
		heap[elem]->previewCount = PREVIEW;
		if (elem < elementCount) {
			offset dummy;
			if (!elementLists[elem]->getFirstStartBiggerEq(0, &heap[elem]->next, &dummy))
				heap[elem]->next = MAX_OFFSET;
		}
		else
			heap[elem]->next = MAX_OFFSET;
	}
	qsort(heap, elementCount, sizeof(LHS*), lhsComparator);

	// ----- BEGIN MAXSCORE -----
	int termWithLeastImpact = -1;
	float maxImpactOfTermWithLeastImpact = 1E6;
	if (useMaxScore) {
		for (int i = 0; i < elementCount; i++) {
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
			float maxImpact = internalWeights[i] * 2.2 * (1.0 - 0.5 / (1 << BITS_PER_SCORE_CONTRIB));
#else
			float maxImpact = internalWeights[i] * (k1 + 1.0);
#endif
			if (maxImpact < maxImpactOfTermWithLeastImpact) {
				termWithLeastImpact = i;
				maxImpactOfTermWithLeastImpact = maxImpact;
			}
		}
	}
	int eliminatedTerms[MAX_SCORER_COUNT];
	int eliminatedTermCount = 0;
	float maxImpactOfEliminatedTerms = 0.0;
	// ----- END MAXSCORE -----

	int tf[MAX_SCORER_COUNT];
	int whichScorer[MAX_SCORER_COUNT];

	// this is the lowest score on the heap
	float worstScore = 0.0;

	while (heap[0]->next < MAX_OFFSET) {
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
		static const offset BIT_MASK_FOR_SCORE_CONTRIB = ((1 << SHIFT_FOR_SCORE_CONTRIB) - 1);
		offset where = (heap[0]->next | BIT_MASK_FOR_SCORE_CONTRIB);
		if (where >= DOCUMENT_COUNT_OFFSET)
			break;
#else
		offset where = (heap[0]->next | DOC_LEVEL_MAX_TF);
		if (where >= DOCUMENT_COUNT_OFFSET)
			break;
		float dl = doclens[getDocIdFromPosting(where)];
		float K = k1 * (1 - b + b * dl / avgdl);
#endif

		float score = 0.0;

		// loop over all query terms appearing in the current document and compute
		// the document's score
		do {
			int who = heap[0]->who;
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
			int encodedScore = (int)(heap[0]->next & BIT_MASK_FOR_SCORE_CONTRIB);
			float scoreContrib = (encodedScore + 0.5f) * (2.2f / (1 << BITS_PER_SCORE_CONTRIB));
			score += internalWeights[who] * scoreContrib;
#else
			int encodedTermFrequency = (int)(heap[0]->next & DOC_LEVEL_MAX_TF);
			float tf = decodeDocLevelTF(encodedTermFrequency);
			score += internalWeights[who] * (tf * (k1 + 1.0f)) / (tf + K);
#endif

			// remove head of queue for current top element; load new data from
			// list if necessary
			if (heap[0]->previewPos < heap[0]->previewCount)
				heap[0]->next = heap[0]->preview[heap[0]->previewPos++];
			else {
				if (heap[0]->previewCount >= PREVIEW) {
					heap[0]->previewPos = 0;
					heap[0]->previewCount =
						elementLists[who]->getNextN(where + 1, MAX_OFFSET, PREVIEW, heap[0]->preview, dummy);
					if (heap[0]->previewCount > 0)
						heap[0]->next = heap[0]->preview[heap[0]->previewPos++];
					else
						heap[0]->next = MAX_OFFSET;
				}
				else
					heap[0]->next = MAX_OFFSET;
			}

			// perform a reheap operation on the scorer heap
			if (elementCount <= 3) {
				LHS *orig = heap[0];
				if (heap[1]->next < orig->next) {
					heap[0] = heap[1];
					if (heap[2]->next < orig->next) {
						heap[1] = heap[2];
						heap[2] = orig;
					}
					else
						heap[1] = orig;
				}
			}
			else {
				LHS *orig = heap[0];
				int node = 0, leftChild = 1, rightChild = 2;
				while (true) {
					int child = leftChild;
					if (heap[rightChild]->next < heap[leftChild]->next)
						child = rightChild;
					if (orig->next <= (heap[child]->next | DOC_LEVEL_MAX_TF))
						break;
					heap[node] = heap[child];
					node = child;
					leftChild = node + node + 1;
					rightChild = node + node + 2;
				}
				heap[node] = orig;
			}
		} while (heap[0]->next <= where);

	 	// we have a heap structure that contains the best "count" containers; only add
		// the current candidate to the heap if it can beat the worst extent on the heap
		if (score + maxImpactOfEliminatedTerms > worstScore) {
			// Process terms that have been removed from the heap my MaxScore.
			for (int i = 0; i < eliminatedTermCount; i++) {
				int who = eliminatedTerms[i];
				offset s, e;
				if (elementLists[who]->getFirstStartBiggerEq(where ^ DOC_LEVEL_MAX_TF, &s, &e)) {
					if (s <= where) {
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
						int encodedScore = (int)(s & BIT_MASK_FOR_SCORE_CONTRIB);
						float scoreContrib = (encodedScore + 0.5f) * (2.2f / (1 << BITS_PER_SCORE_CONTRIB));
						score += internalWeights[who] * scoreContrib;
#else
						int encodedTermFrequency = (int)(s & DOC_LEVEL_MAX_TF);
						float tf = decodeDocLevelTF(encodedTermFrequency);
						score += internalWeights[who] * (tf * (k1 + 1.0f)) / (tf + K);
#endif
					}
				}
			}
			if (score <= worstScore)
				continue;

			sex.score = score;
			sex.from = getDocIdFromPosting(where);
			sex.to = sex.from;
			if (sexCount < count) {
				sexes[sexCount++] = sex;
				if (sexCount >= count) {
					sortResultsByScore(sexes, sexCount, true);
					for (int i = count; i < count * 2 + 2; i++)
						sexes[i].score = 999999.999;  // sentinels
					worstScore = sexes[0].score;
				}
			}
			else {
				// insert the new ScoredExtent instance into the heap; perform a reheap operation
				// in order to have the lowest-scoring candidate on the top of the heap again
				int node = 0, child = 1;
				while (true) {
					if (sexes[child + 1].score < sexes[child].score)
						child = child + 1;
					if (sexes[child].score >= sex.score)
						break;
					sexes[node] = sexes[child];
					node = child;
					child = node + node + 1;
				}
				sexes[node] = sex;
				worstScore = sexes[0].score;

				if (worstScore >= maxImpactOfEliminatedTerms + maxImpactOfTermWithLeastImpact) {
					// MaxScore heuristic: Remove term with least impact from the heap.
					for (int i = 0; i < elementCount; ++i) {
						if (heap[i]->who == termWithLeastImpact)
							heap[i]->next = MAX_OFFSET;
					}
					// Restore the heap property.
					qsort(heap, elementCount, sizeof(LHS*), lhsComparator);

					maxImpactOfEliminatedTerms += maxImpactOfTermWithLeastImpact;
					eliminatedTerms[eliminatedTermCount++] = termWithLeastImpact;

					// Find the next term we should remove from the heap.
					termWithLeastImpact = -1;
					maxImpactOfTermWithLeastImpact = 1E6;
					for (int i = 0; i < elementCount; ++i) {
						if (heap[i]->next != MAX_OFFSET) {
							int who = heap[i]->who;
#if INDEX_CONTAINS_PRECOMPUTED_SCORES
							float maxImpact = internalWeights[who] * 2.2 * (1.0 - 0.5 / (1 << BITS_PER_SCORE_CONTRIB));
#else
							float maxImpact = internalWeights[who] * (k1 + 1.0);
#endif
							if (maxImpact < maxImpactOfTermWithLeastImpact) {
								termWithLeastImpact = who;
								maxImpactOfTermWithLeastImpact = maxImpact;
							}
						}
					}
				}
			}
		}

	} // end while (heap[0]->next < MAX_OFFSET)

	for (int elem = 0; elem < elementCount * 2 + 2; elem++)
		free(heap[elem]);
	free(heap);

	if (sexCount < count)
		count = sexCount;
	results = sexes;
	sortResultsByScore(results, count, false);

	// Since we use positionless indexing, the "from" component of each result extent
	// only contains a document number, not an actual offset. We need to translate
	// that into start/end offsets for the respective document.
	for (int i = 0; i < count; i++)
		containerList->getNth(results[i].from, &results[i].from, &results[i].to);
} // end of executeQuery_DocumentAtATime()


void Chapter6::executeQuery_TermAtATime() {
	assert(!INDEX_CONTAINS_PRECOMPUTED_SCORES);

	if (count <= 0) {
		results = typed_malloc(ScoredExtent, 1);
		count = 0;
		return;
	}

	ExtentList* containerList = containerQuery->getResult();
	double containerCount = containerList->getLength();
	ExtentList *elementLists[MAX_SCORER_COUNT];

	for (int i = 0; i < elementCount; i++)
		elementLists[i] = elementQueries[i]->getResult();
	computeTermWeights(elementLists, containerCount);

	float* doclens;
	float avgdl;
 	DocLenCache::getDocLens(containerList, &doclens, &avgdl);

	// sort scorers by their lengths and store the new ordering in whichScorer
	int whichScorer[MAX_SCORER_COUNT];
	for (int i = 0; i < elementCount; i++)
		whichScorer[i] = i;
	for (int i = 0; i < elementCount; i++) {
		int best = i;
		for (int k = i + 1; k < elementCount; k++)
			if (elementLists[whichScorer[k]]->getLength() < elementLists[whichScorer[best]]->getLength())
				best = k;
		int tmp = whichScorer[i];
		whichScorer[i] = whichScorer[best];
		whichScorer[best] = tmp;
	}
	for (int i = 0; i < elementCount - 1; i++)
		assert(elementLists[whichScorer[i]]->getLength() <= elementLists[whichScorer[i + 1]]->getLength());

	struct Accumulator {
		offset docid;
		float score;
		float K;
	};
#define initializeAccumulator(p, w, a) { \
	offset docid = (p >> DOC_LEVEL_SHIFT); \
	a->docid = docid; \
	float K = k1 * (1 - b + b * doclens[docid] / avgdl); \
	a->K = K; \
	int encodedTermFrequency = (p & DOC_LEVEL_MAX_TF); \
	float tf = decodeDocLevelTF(encodedTermFrequency); \
	a->score = w * tf * (k1 + 1.0f) / (tf + K); \
}
#define updateAccumulator(p, w, a) { \
	int encodedTermFrequency = (p & DOC_LEVEL_MAX_TF); \
	float tf = decodeDocLevelTF(encodedTermFrequency); \
	a->score += w * tf * (k1 + 1.0f) / (tf + a->K); \
}

	Accumulator *accumulators = typed_malloc(Accumulator, accumulatorLimit);
	int accumulatorsUsed = 0;

	for (int i = 0; i < elementCount; i++) {
		int who = whichScorer[i];
		ExtentList *list = elementLists[who];
		int length = list->getLength();
		float weight = internalWeights[who];

		float maxImpactLeft = 0.0f;
		for (int k = i; k < elementCount; k++)
			maxImpactLeft += internalWeights[whichScorer[k]] * (k1 + 1.0f);
		int numAccumulatorsAboveMaxImpactLeft = 0;

		Accumulator *newAccumulators = accumulators;
		int newAccumulatorsUsed = accumulatorsUsed;

		const int CHUNK_SIZE = 128;	
		offset postings[CHUNK_SIZE];
		int inPos = 0, outPos = 0;

		if (accumulatorsUsed + length <= accumulatorLimit) {
			// This is the easy case, where all accumulators for the current list
			// will fit into the array.
			if (accumulatorsUsed > 0)
				newAccumulators = typed_malloc(Accumulator, accumulatorLimit);
		
			offset where = 0;
			int left = length, n;
			while (n = list->getNextN(where, MAX_OFFSET, (left > CHUNK_SIZE ? CHUNK_SIZE : left), postings, postings)) {
				left -= n;
				for (int k = 0; k < n; k++) {
					offset posting = postings[k];
					if (posting >= DOCUMENT_COUNT_OFFSET)
						break;
					offset docid = (posting >> DOC_LEVEL_SHIFT);
					while ((inPos < accumulatorsUsed) && (accumulators[inPos].docid < docid))
						newAccumulators[outPos++] = accumulators[inPos++];

					Accumulator *acc = &newAccumulators[outPos++];
					if ((inPos < accumulatorsUsed) && (accumulators[inPos].docid == docid)) {
						// update existing accumulator
						*acc = accumulators[inPos++];
						updateAccumulator(posting, weight, acc);
						if ((useMaxScore) && (acc->score > maxImpactLeft)) {
							if (++numAccumulatorsAboveMaxImpactLeft >= count)
								accumulatorLimit = newAccumulatorsUsed;
						}
					}
					else {
						// create new accumulator
						newAccumulatorsUsed++;
						initializeAccumulator(posting, weight, acc);
					}
				}
				where = postings[n - 1] + 1;
				if (newAccumulatorsUsed >= accumulatorLimit)
					break;
			}
		}
		else if (accumulatorsUsed < accumulatorLimit) {
			// This is the difficult case, where we have to prune accumulators.

			if (accumulatorsUsed > 0)
				newAccumulators = typed_malloc(Accumulator, accumulatorLimit);

#define ADAPTIVE_PRUNING 1
#if ADAPTIVE_PRUNING
			int minTfForNewAccumulator = 1;
			int tfCounts[DOC_LEVEL_MAX_TF + 1];
			memset(tfCounts, 0, sizeof(tfCounts));
			int chunksDone = 0;
#endif
			
			offset where = 0;
			int left = length, n;
			while (n = list->getNextN(where, MAX_OFFSET, (left > CHUNK_SIZE ? CHUNK_SIZE : left), postings, postings)) {
				left -= n;
#if ADAPTIVE_PRUNING
				chunksDone++;
#endif
				for (int k = 0; k < n; k++) {
					offset posting = postings[k];
					if (posting >= DOCUMENT_COUNT_OFFSET)
						break;
					offset docid = (posting >> DOC_LEVEL_SHIFT);
					while ((inPos < accumulatorsUsed) && (accumulators[inPos].docid < docid))
						newAccumulators[outPos++] = accumulators[inPos++];

					if ((inPos < accumulatorsUsed) && (accumulators[inPos].docid == docid)) {
						Accumulator *acc = &newAccumulators[outPos++];
						*acc = accumulators[inPos++];
						updateAccumulator(posting, weight, acc);
						if ((useMaxScore) && (acc->score > maxImpactLeft)) {
							if (++numAccumulatorsAboveMaxImpactLeft >= count)
								accumulatorLimit = newAccumulatorsUsed;
						}
					}
					else if (newAccumulatorsUsed < accumulatorLimit) {
#if ADAPTIVE_PRUNING
						int tf = (posting & DOC_LEVEL_MAX_TF);
						tfCounts[MIN(15, tf)]++;
						if (tf >= minTfForNewAccumulator) {
							newAccumulatorsUsed++;
							Accumulator *acc = &newAccumulators[outPos++];
							initializeAccumulator(posting, weight, acc);
						}
#else
						newAccumulatorsUsed++;
						Accumulator *acc = &newAccumulators[outPos++];
						initializeAccumulator(posting, weight, acc);
#endif
					}
				}
				where = postings[n - 1] + 1;
				if (newAccumulatorsUsed >= accumulatorLimit)
					break;
#if ADAPTIVE_PRUNING
				double numChunksLeft = left / CHUNK_SIZE;
				double accumulatorsLeft = accumulatorLimit - newAccumulatorsUsed;
				double oneOverChunksDone = 1.0 / chunksDone;
				double allowedPerChunk = accumulatorsLeft / (numChunksLeft + 0.5);
				double sum = 0.0;
				minTfForNewAccumulator = 1;
				for (int t = 15; t >= 1; t--) {
					sum += tfCounts[t] * oneOverChunksDone;
					if (sum > allowedPerChunk) {
						minTfForNewAccumulator = t + 1;
						break;
					}
				}
#endif
			}
		}

		// Process all remaining in-accumulators.
		if (inPos < accumulatorsUsed) {
			offset s = 0, e;
			if (accumulators == newAccumulators) {
				while (inPos < accumulatorsUsed) {
					Accumulator *acc = &accumulators[inPos++];
					offset where = (acc->docid << DOC_LEVEL_SHIFT);
					if (!list->getFirstStartBiggerEq(where, &s, &e))
						break;
					else if (s <= where + DOC_LEVEL_MAX_TF)
						updateAccumulator(s, weight, acc);
				}
			}
			else {
				while (inPos < accumulatorsUsed) {
					Accumulator *acc = &newAccumulators[outPos++];
					*acc = accumulators[inPos++];
					offset where = (acc->docid << DOC_LEVEL_SHIFT);
					if (s <= where + DOC_LEVEL_MAX_TF) {
						if (!list->getFirstStartBiggerEq(where, &s, &e))
							s = MAX_OFFSET;
						else if (s <= where + DOC_LEVEL_MAX_TF)
							updateAccumulator(s, weight, acc);
					}
				}
			}
		}

		if (newAccumulators != accumulators) {
			free(accumulators);
			accumulators = newAccumulators;
		}
		accumulatorsUsed = newAccumulatorsUsed;
	}

	float worstScore = 0.0f;
	results = typed_malloc(ScoredExtent, 2 * count + 2);
	int resultCount = 0;
	for (int i = 0; i < accumulatorsUsed; i++) {
		float score = accumulators[i].score;
		if (score <= worstScore)
			continue;

		if (resultCount < count) {
			ScoredExtent *result = &results[resultCount++];
			result->score = score;
			result->from = accumulators[i].docid;
			result->to = result->from;

			if (resultCount >= count) {
				sortResultsByScore(results, resultCount, true);
				for (int k = count; k < count * 2 + 2; k++)
					results[k].score = 999999.999;  // sentinels
				worstScore = results[0].score;
			}
		}
		else {
			// insert the new ScoredExtent instance into the heap; perform a reheap operation
			// in order to have the lowest-scoring candidate on the top of the heap again
			int node = 0, child = 1;
			while (true) {
				if (results[child + 1].score < results[child].score)
					child = child + 1;
				if (results[child].score >= score)
					break;
				results[node] = results[child];
				node = child;
				child = node + node + 1;
			}
			ScoredExtent *result = &results[node];
			result->score = score;
			result->from = accumulators[i].docid;
			result->to = result->from;

			worstScore = results[0].score;
		}
	}
	free(accumulators);

	count = resultCount;
	sortResultsByScore(results, count, false);

	// Since we use positionless indexing, the "from" component of each result extent
	// only contains a document number, not an actual offset. We need to translate
	// that into start/end offsets for the respective document.
	for (int i = 0; i < count; i++)
		containerList->getNth(results[i].from, &results[i].from, &results[i].to);
} // end of executeQuery_TermAtATime()



void Chapter6::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	k1 = getModifierDouble(modifiers, "k1", 1.2);
	b = getModifierDouble(modifiers, "b", 0.75);
	conjunctive = getModifierBool(modifiers, "conjunctive", false);
	termAtATime = getModifierBool(modifiers, "term_at_a_time", false);
	useMaxScore = getModifierBool(modifiers, "use_max_score", false);
	accumulatorLimit = getModifierInt(modifiers, "accumulator_limit", 100000);
	ntoulas = getModifierBool(modifiers, "ntoulas", false);
} // end of processModifiers(char**)


