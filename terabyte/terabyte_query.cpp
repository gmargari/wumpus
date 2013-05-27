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
 * Implementation of the generic Okapi BM25 scoring function, fine-tuned for
 * high query processing performance, targeted towards TREC Terabyte.
 *
 * author: Stefan Buettcher
 * created: 2004-09-27
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <map>
#include <set>
#include "terabyte_query.h"
#include "terabyte_surrogates.h"
#include "../extentlist/simplifier.h"
#include "../feedback/feedback.h"
#include "../feedback/incomplete_language_model.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../index/compactindex.h"
#include "../index/compactindex2.h"
#include "../indexcache/docidcache.h"
#include "../indexcache/extentlist_cached.h"
#include "../misc/all.h"
#include "../query/getquery.h"
#include "../query/querytokenizer.h"
#include "../stemming/stemmer.h"


static const char * LOG_ID = "TerabyteQuery";

static const int MEMORY_LIMIT_PER_TERM = 64 * 1024 * 1024;


/**
 * This is an ad hoc solution: In order to be able to access the in-memory index
 * (used for increased QP performance, we have class variable that points to the
 * in-memory index. The index is located at the position given here.
 **/
CompactIndex * TerabyteQuery::inMemoryIndex = NULL;
bool TerabyteQuery::mustLoadInMemoryIndex = true;


void TerabyteQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	isDocumentLevel = false;
	pseudoRelevanceFeedback = FEEDBACK_NONE;
	surrogateMode = RERANK_SURROGATE_NONE;
	BM25Query::initialize(index, command, modifiers, body, visibleExtents, memoryLimit);

	// load in-memory index if the configuration file tells us so
	if ((inMemoryIndex == NULL) && (mustLoadInMemoryIndex)) {
		char inMemoryIndexFile[MAX_CONFIG_VALUE_LENGTH];
		if (getConfigurationValue("TERABYTE_IN_MEMORY_INDEX", inMemoryIndexFile)) {
			struct stat buf;
			if (stat(inMemoryIndexFile, &buf) == 0) {
				log(LOG_DEBUG, LOG_ID, "Loading index into memory. This may take a while...");
				if (CompactIndex2::canRead(inMemoryIndexFile))
					inMemoryIndex = new CompactIndex2(NULL, inMemoryIndexFile);
				else
					inMemoryIndex = new CompactIndex(NULL, inMemoryIndexFile);
				log(LOG_DEBUG, LOG_ID, "Index loaded.");
			}
		}
		mustLoadInMemoryIndex = false;
	} // end if ((inMemoryIndex == NULL) && (mustLoadInMemoryIndex))

	getConfigurationBool("POSITIONLESS_INDEXING", &positionless, false);
	elementCount = 0;
	memset(elementQueries, 0, sizeof(elementQueries));
} // end of initialize(...)


TerabyteQuery::TerabyteQuery(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	mustFreeVisibleExtentsInDestructor = false;
} // end of TerabyteQuery(...)


TerabyteQuery::TerabyteQuery(Index *index, const char *command, const char **modifiers,
		const char *body, uid_t userID, int memoryLimit) {
	if (index->APPLY_SECURITY_RESTRICTIONS)
		this->userID = userID;
	else
		this->userID = Index::GOD;
	char *addGet = getModifierString(modifiers, "addget", NULL);
	if (addGet != NULL)
		free(addGet);
	if ((this->userID == Index::GOD) && (addGet == NULL))
		initialize(index, command, modifiers, body, NULL, memoryLimit);
	else {
		VisibleExtents *visibleExtents =
			index->getVisibleExtents(this->userID, false);
		initialize(index, command, modifiers, body, visibleExtents, memoryLimit);
	}
	mustFreeVisibleExtentsInDestructor = true;
} // end of TerabyteQuery(...)


TerabyteQuery::~TerabyteQuery() {
} // end of ~TerabyteQuery()


void TerabyteQuery::setScorers(ExtentList **scorers, int scorerCount) {
	elementCount = scorerCount;
	for (int i = 0; i < scorerCount; i++) {
		externalWeights[i] = 1.0;
		elementQueries[i] = new GCLQuery(index, scorers[i]);
		elementQueries[i]->parse();
	}
	isDocumentLevel = true;
} // end of setScorers(ExtentList**, int)


static ExtentList *fetchPostingsFromInMemoryIndex(
		Index *index, CompactIndex *inMemoryIndex, char *term) {
	if (inMemoryIndex == NULL)
		return NULL;

	// add "$" symbol to end of term if required by Index's stemming level
	int termLen = strlen(term);
	char term2[MAX_TOKEN_LENGTH * 2];
	strcpy(term2, term);
	if ((index->STEMMING_LEVEL > 2) && (term[termLen - 1] != '$')) {
		sprintf(term2, "%s$", term);
		termLen++;
	}

	// if term needs to be stemmed, perform stemming
	if (term2[termLen - 1] == '$') {
		char stem[MAX_TOKEN_LENGTH * 2];
		strcpy(stem, term2);
		stem[termLen - 1] = 0;
		Stemmer::stem(stem, LANGUAGE_ENGLISH, false);
		if (stem[0] == 0)
			term2[--termLen] = 0;
		else
			sprintf(term2, "%s$", stem);
	}

	ExtentList *result = inMemoryIndex->getPostings(term2);
#if 1
	if (result->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
		// Empty list means the term does not exist in the in-memory index: Return NULL.
		// This will make the query processor fetch the posting list from the on-disk
		// index. If you want to use the in-memory index exclusively, disable this piece
		// of code.
		delete result;
		return NULL;
	}
#endif
	return result;
} // end of fetchPostingsFromInMemoryIndex(char*)


static void *createTerabyteElementQuery(void *data) {
	TerabyteQueryTerm *tqt = (TerabyteQueryTerm*)data;
	Index *index = tqt->index;
	tqt->fromInMemoryIndex = false;

	if (tqt->isDocumentLevel) {
		char *qs = tqt->query->getQueryString();
		InputToken token;
		XMLInputStream *tokenizer = new XMLInputStream(qs, strlen(qs), true);
		tokenizer->getNextToken(&token);
		delete tokenizer;
		free(qs);

#if 0
		LanguageModel *collectionModel = index->getStaticLanguageModel();
		assert(collectionModel != NULL);
		if (collectionModel->getTermProbability((char*)token.token) < 1E-10)
			return new ExtentList_Empty();
#endif

		char term[MAX_TOKEN_LENGTH * 2];
		if ((char)token.token[0] == '$')
			sprintf(term, "<!>%s$", (char*)&token.token[1]);
		else
			sprintf(term, "<!>%s", (char*)token.token);

		// consult the in-memory index; maybe we have data available there
		ExtentList *list = NULL;
		if (tqt->inMemoryIndex != NULL) {
			list = fetchPostingsFromInMemoryIndex(index, tqt->inMemoryIndex, term);
			if (list != NULL)
				tqt->fromInMemoryIndex = true;
		}

		if (list == NULL)
			list = index->getPostings(term, Index::GOD);
		if (list != NULL)
			tqt->query->setResultList(Simplifier::simplifyList(list));
		else
			tqt->query->setResultList(new ExtentList_Empty());
	} // end if (tqt->isDocumentLevel);

	tqt->query->parse();
	return NULL;
} // end of createTerabyteElementQuery(void*)


/**
 * A BM25 query (as any ranked query) has to look like:
 * @rank[...] CONTAINER by ELEM1, ELEM2, ...
 * This method splits the query string into its ingredients.
 **/
bool TerabyteQuery::parse() {
	if (!parseQueryString(queryString, NULL, NULL, memoryLimit)) {
		syntaxErrorDetected = finished = true;
		ok = false;
	}
	else if ((containerQuery != NULL) || (statisticsQuery != NULL)) {
		syntaxErrorDetected = finished = true;
		ok = false;
	}
	else if (elementCount > 30) {
		syntaxErrorDetected = finished = true;
		ok = false;
	}
	else {
		containerQuery =
			new GCLQuery(index, "gcl", EMPTY_MODIFIERS, DOC_QUERY, visibleExtents, memoryLimit);
		if (!containerQuery->parse()) {
			syntaxErrorDetected = finished = true;
			ok = false;
		}
		else {
			statisticsQuery = containerQuery;
			processQuery();
			ok = true;
		}
	}
	return ok;
} // end of parse()


typedef struct {
	SegmentedPostingList *list;
	bool terminate;
} TerabyteDecompressionStruct;


void *decompressListConcurrently(void *data) {
	TerabyteDecompressionStruct *tds = (TerabyteDecompressionStruct*)data;
	offset *uncompressed = typed_malloc(offset, MAX_SEGMENT_SIZE);
	int decompressedCnt = 0;
	for (int i = 2; i < tds->list->IN_MEMORY_SEGMENT_COUNT; i++) {
		if (tds->list->compressedSegments[i].postings == NULL)
			continue;
		sched_yield();
		if (tds->terminate)
			break;
		int listLen;
		decompressList(tds->list->compressedSegments[i].postings,
				tds->list->compressedSegments[i].byteLength, &listLen, uncompressed);
		assert(listLen == tds->list->compressedSegments[i].count);
		free(tds->list->compressedSegments[i].postings);
		tds->list->compressedSegments[i].postings = compressNone(
				uncompressed, listLen, &tds->list->compressedSegments[i].byteLength);
		decompressedCnt++;
	}
	free(uncompressed);
	return NULL;
} // end of decompressListConcurrently(void*)


bool TerabyteQuery::parseScorers(const char *scorers, int memoryLimit) {
	if (elementCount > 0)
		return true;

	QueryTokenizer *tok = new QueryTokenizer(scorers);
	elementCount = tok->getTokenCount();
	TerabyteQueryTerm *queryTerms = typed_malloc(TerabyteQueryTerm, elementCount);
	bool returnValue = true;

	// if we have two-phase query processing, the first phase is ALWAYS doclevel
	isDocumentLevel = (index->DOCUMENT_LEVEL_INDEXING > 0);

	for (int i = 0; i < elementCount; i++) {
		char *token = tok->getNext();
		elementQueries[i] = createElementQuery(token, &externalWeights[i], MEMORY_LIMIT_PER_TERM);
		char *qs = elementQueries[i]->getQueryString();
		if (!GCLQuery::isSimpleTerm(qs))
			isDocumentLevel = false;
		free(qs);
		elementQueries[i]->almostSecureWillDo();
		queryTerms[i].index = index;
		queryTerms[i].inMemoryIndex = inMemoryIndex;
		queryTerms[i].query = elementQueries[i];
	}
	delete tok;

	// fetch all posting lists sequentially
	for (int i = 0; i < elementCount; i++) {
		queryTerms[i].isDocumentLevel = isDocumentLevel;
		createTerabyteElementQuery(&queryTerms[i]);
		if (!elementQueries[i]->parse())
			returnValue = false;
	}
	free(queryTerms);

#if 0
	for (int i = 0; i < elementCount; i++) {
		if (elementQueries[i]->getResult()->getType() != ExtentList::TYPE_SEGMENTEDPOSTINGLIST)
			continue;
		SegmentedPostingList *spl =
			(SegmentedPostingList*)elementQueries[i]->getResult();
		if ((!isDocumentLevel) && (spl->getLength() > 2000000))
			spl->initialize();
		else {
			PostingList *pl =
				new PostingList(spl->toArray(), spl->getLength(), false, true);
			elementQueries[i]->setResultList(pl);
		}
	}
#endif

	if (elementCount == 0)
		return false;
	else
		return returnValue;
} // end of parseScorers(char*, int)


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


static bool stemEquivalent(char *t1, char *t2) {
	int outPos;
	char *s1 = duplicateString(t1);
	outPos = 0;
	for (int i = 0; s1[i] != 0; i++)
		if ((s1[i] != '$') && (s1[i] != '"') && (s1[i] != ' '))
			s1[outPos++] = s1[i];
	s1[outPos] = 0;
	outPos = 0;
	char *s2 = duplicateString(t2);
	for (int i = 0; s2[i] != 0; i++)
		if ((s2[i] != '$') && (s2[i] != '"') && (s2[i] != ' '))
			s2[outPos++] = s2[i];
	s2[outPos] = 0;
	char stemmed1[MAX_TOKEN_LENGTH * 2], stemmed2[MAX_TOKEN_LENGTH * 2];
	Stemmer::stemWord(s1, stemmed1, LANGUAGE_ENGLISH, false);
	Stemmer::stemWord(s2, stemmed2, LANGUAGE_ENGLISH, false);
	bool result;
	if ((stemmed1[0] == 0) || (stemmed2[0] == 0))
		result = (strcmp(s1, s2) == 0);
	else
		result = (strcmp(stemmed1, stemmed2) == 0);
	free(s1);
	free(s2);
	return result;
} // end of stemEquivalent(char*, char*)


void TerabyteQuery::processCoreQuery() {
	int originalCount = count;
	if (isDocumentLevel)
		executeQueryDocLevel();
	else
		executeQueryWordLevel();
} // end of processCoreQuery()


inline void moveScorerHeapNodeDown(LHS **heap, int node) {
	LHS *orig = heap[node];
	int child = node + node + 1;
	while (true) {
		if (heap[child + 1]->next < heap[child]->next)
			child++;
		if (orig->next <= heap[child]->next)
			break;
		heap[node] = heap[child];
		node = child;
		child = node + node + 1;
	}
	heap[node] = orig;
} // end of moveScorerHeapNodeDown(LHS*, int)


void TerabyteQuery::computeCollectionStats(ExtentList *containerList, IndexCache *cache) {
	offset containerPreviewStart[PREVIEW + 1], containerPreviewEnd[PREVIEW + 1];
	int sizeOfCachedStats;
	TerabyteCachedDocumentStatistics *cachedStats = (TerabyteCachedDocumentStatistics*)
		cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	if (cachedStats == NULL) {
		cachedStats = typed_malloc(TerabyteCachedDocumentStatistics, 1);
		cache->addMiscDataToCache("TB_COLLECTION_STATS", cachedStats,
				sizeof(TerabyteCachedDocumentStatistics), false);
		free(cachedStats);
		cachedStats = (TerabyteCachedDocumentStatistics*)
			cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	}

	cachedStats->k1 = k1;
	cachedStats->b = b;

	// loop over all documents; accumulate count and total size
	unsigned int containerCount = 0;
	offset avgContainerLength = 0;
	int previewSize = containerList->getNextN(
			0, MAX_OFFSET, PREVIEW, containerPreviewStart, containerPreviewEnd);
	while (previewSize > 0) {
		containerCount += previewSize;
		for (int i = 0; i < previewSize; i++) {
			offset dl = (containerPreviewEnd[i] - containerPreviewStart[i] + 1);
			avgContainerLength += dl;
		}
		previewSize = containerList->getNextN(containerPreviewStart[previewSize - 1] + 1,
				MAX_OFFSET, PREVIEW, containerPreviewStart, containerPreviewEnd);
	} // end while (previewSize > 0)

	// precompute collection statistics and TF impact values and put everything
	// into the cache
	double averageContainerLength = avgContainerLength;
	if (containerCount > 0)
		averageContainerLength /= containerCount;
	else
		averageContainerLength = 1.0;
	cachedStats->documentCount = containerCount;
	cachedStats->avgDocumentLength = averageContainerLength;
	int avgdl = (int)averageContainerLength;
	int shift = 0;
	while (avgdl > MAX_CACHED_SHIFTED_DL/8) {
		avgdl >>= 1;
		shift++;
	}
	cachedStats->documentLengthShift = shift;
	for (int dl = 0; dl <= MAX_CACHED_SHIFTED_DL; dl++) {
		float K = k1 * ((1 - b) + b * (dl << shift) / averageContainerLength);
		for (int tf = 0; tf <= MAX_CACHED_TF; tf++) {
			double TF = decodeDocLevelTF(tf);
			float impact = (k1 + 1.0) * TF / (K + TF);
			cachedStats->tfImpactValue[dl][tf] = impact;
		}
	}

	int dummy;
	if ((positionless) &&
	    (cache->getPointerToMiscDataFromCache("TB_DOCUMENT_LENGTHS", &dummy) == NULL)) {
		// save relative document lengths in the cache
		uint16_t *docLens = typed_malloc(uint16_t, containerCount);
		unsigned int outPos = 0;
		previewSize = containerList->getNextN(
				0, MAX_OFFSET, PREVIEW, containerPreviewStart, containerPreviewEnd);
		while (previewSize > 0) {
			for (int i = 0; i < previewSize; i++) {
				offset dl = (containerPreviewEnd[i] - containerPreviewStart[i] + 1) >> shift;
				if (dl > 64000)
					dl = 64000;
				docLens[outPos++] = dl;
			}
			previewSize = containerList->getNextN(containerPreviewStart[previewSize - 1] + 1,
					MAX_OFFSET, PREVIEW, containerPreviewStart, containerPreviewEnd);
		} // end while (previewSize > 0)
		assert(outPos == containerCount);
		cache->addMiscDataToCache("TB_DOCUMENT_LENGTHS", docLens,
				containerCount * sizeof(uint16_t), false);
		free(docLens);
	}
} // end of computeCollectionStats(ExtentList*, IndexCache*)


void TerabyteQuery::executeQueryDocLevel() {
	if (count <= 0) {
		results = typed_malloc(ScoredExtent, 1);
		count = 0;
		return;
	}

	offset start, end, s, e, containerLength;
	unsigned int containerCount = 0;
	offset avgContainerLength = 0;
	float averageContainerLength = 1.0;
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();

	// check whether we can use cached collection statistics
	IndexCache *cache = index->getCache();
	assert(cache != NULL);
	int sizeOfCachedStats;
	TerabyteCachedDocumentStatistics *cachedStats = (TerabyteCachedDocumentStatistics*)
		cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	if (cachedStats == NULL) {
		// no cached collection statistics available yet; compute average document length
		// etc. and store the results in the index cache
		computeCollectionStats(containerList, cache);
		cachedStats = (TerabyteCachedDocumentStatistics*)
			cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	}
	else if ((k1 != cachedStats->k1) || (b != cachedStats->b))
		computeCollectionStats(containerList, cache);
	assert(cachedStats != NULL);

	containerCount = cachedStats->documentCount;
	averageContainerLength = cachedStats->avgDocumentLength;
	int sizeOfDocumentLengths;
	uint16_t *documentLengths = NULL;
	if (positionless) {
		documentLengths = (uint16_t*)
			cache->getPointerToMiscDataFromCache("TB_DOCUMENT_LENGTHS", &sizeOfDocumentLengths);
		assert(documentLengths != NULL);
		assert(sizeOfDocumentLengths == containerCount * sizeof(uint16_t));
	}

	// initialize heap structure for result extents
	ScoredExtent sex;
	ScoredExtent *sexes = typed_malloc(ScoredExtent, 2 * count + 2);
	int sexCount = 0;
	offset dummy[PREVIEW + 2];

	// compute the BM25 term weight for all elements
	offset totalLength = 0;
	for (int i = 0; i < elementCount; i++) {
		elementLists[i] = elementQueries[i]->getResult();
		offset lastStart, lastEnd;
		offset listLength = elementLists[i]->getLength();
		totalLength += listLength;
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

#if 0
	if ((positionless) && (totalLength <= 100000)) {
		free(sexes);
		executeQueryDocLevel_TermAtATime();
		return;
	}
#endif

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

	// additional pointer variable for speedup
	float *maxPossibleImpact = cachedStats->tfImpactValue[0];
	int dlShift = cachedStats->documentLengthShift;

	// We keep a list of sucker terms that are too weak to make much of a difference.
	// These terms are removed from the heap of query terms and only looked at when
	// necessary.
	static const int MAX_SUCKER_TERM_COUNT = 3;
	int suckerTermCount = 0;   // number of sucker terms found so far
	int suckerTerms[4];        // list of suckers found so far
	float suckerImpactSoFar = 0.0;   // maximum combined impact of all suckers found so far
	int nextSucker = 0;         // the next candidate to become an official sucker
	for (int i = 1; i < elementCount; i++)
		if (internalWeights[i] < internalWeights[nextSucker])
			nextSucker = i;
	float maxPossibleImpactOfNextSucker =
		internalWeights[nextSucker] * maxPossibleImpact[DOC_LEVEL_MAX_TF];
	float criterionForNextSucker =
		(suckerImpactSoFar + maxPossibleImpactOfNextSucker) * 2.5;

	offset postingsFetched = 0;
	offset lastCallToYield = 0;

	while (heap[0]->next < MAX_OFFSET) {
		offset where = (heap[0]->next | DOC_LEVEL_MAX_TF);

		if (where >= DOCUMENT_COUNT_OFFSET)
			break;

		int scorersInCurrentDocument = 0;
		float maximumPossibleScore = suckerImpactSoFar;

		// loop over all query terms appearing in the current document; store their
		// TF values within the document and the maximum possible score they can
		// produce (not knowing the length of the document)
		do {
			int who = heap[0]->who;
			int termFrequency = (int)(heap[0]->next & DOC_LEVEL_MAX_TF);
			tf[scorersInCurrentDocument] = termFrequency;
			whichScorer[scorersInCurrentDocument++] = who;
			maximumPossibleScore += internalWeights[who] * maxPossibleImpact[termFrequency];

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

		// if the theoretically achievable score for this document (in case doclen=0) is
		// smaller than the top element of the heap, we can directly go to the next document
		if (maximumPossibleScore < worstScore) {

#define MAX_SCORE_HEURISTIC 1
#if MAX_SCORE_HEURISTIC
			if (criterionForNextSucker < worstScore) {
				suckerImpactSoFar += maxPossibleImpactOfNextSucker;

				// remove sucker from the heap
				for (int i = 0; i < elementCount; i++)
					if (heap[i]->who == nextSucker) {
						heap[i]->next = MAX_OFFSET;
						break;
					}
				qsort(heap, elementCount, sizeof(LHS*), lhsComparator);

				// add the new sucker to sucker list
				suckerTerms[suckerTermCount] = nextSucker;
				suckerTermCount++;

				// search for next sucker
				nextSucker = -1;
				for (int i = 0; i < elementCount; i++) {
					bool notASuckerYet = true;
					for (int k = 0; k < suckerTermCount; k++)
						if (suckerTerms[k] == i)
							notASuckerYet = false;
					if (notASuckerYet) {
						if (nextSucker < 0)
							nextSucker = i;
						else if (internalWeights[i] < internalWeights[nextSucker])
							nextSucker = i;
					}
				} // end for (int i = 0; i < elementCount; i++)

				if ((suckerTermCount >= MAX_SUCKER_TERM_COUNT) || (nextSucker < 0))
					criterionForNextSucker = 1.0E+10;
				else {
					maxPossibleImpactOfNextSucker =
						internalWeights[nextSucker] * maxPossibleImpact[DOC_LEVEL_MAX_TF];
					criterionForNextSucker = (suckerImpactSoFar + maxPossibleImpactOfNextSucker) * 2.5;
				}
			} // end [sucker removal]
#endif

			continue;
		} // end if (maximumPossibleScore < worstScore)

		if (positionless) {
			offset n = where / (DOC_LEVEL_MAX_TF + 1);
			start = end = n;
			containerLength = documentLengths[n];
			containerLength <<= dlShift;
		}
		else if (containerList->getFirstEndBiggerEq(where ^ DOC_LEVEL_MAX_TF, &start, &end)) {
			if (start > where)
				continue;
			containerLength = (end - start + 1);
		}
		else
			break;

		sex.score = 0.0;

		// Compute final document score (instead of the upper bound we currently have).
		// Use the impact cache whenever possible (shiftedDL <= MAX_CACHED_SHIFTED_DL).
		// If not possible, compute document score using the Okapi formula.
		int shiftedDL = (containerLength >> dlShift);
		if (shiftedDL <= MAX_CACHED_SHIFTED_DL) {
			float *impact = cachedStats->tfImpactValue[shiftedDL];
			for (int i = 0; i < scorersInCurrentDocument; i++)
				sex.score += internalWeights[whichScorer[i]] * impact[tf[i]];
			if (sex.score + suckerImpactSoFar > worstScore) {
				for (int i = 0; i < suckerTermCount; i++) {
					if (elementLists[suckerTerms[i]]->getLastStartSmallerEq(where, &s, &e))
						if (s >= (where - DOC_LEVEL_MAX_TF)) {
							int TF = (s & DOC_LEVEL_MAX_TF);
							sex.score += internalWeights[suckerTerms[i]] * impact[TF];
						}
				}
			}
			else
				continue;
		}
		else {
			float K = k1 * ((1 - b) + b * containerLength / averageContainerLength);
			for (int i = 0; i < scorersInCurrentDocument; i++) {
				double TF = decodeDocLevelTF(tf[i]);
				sex.score += internalWeights[whichScorer[i]] * (k1 + 1.0) * TF / (K + TF);
			}
			if (sex.score + suckerImpactSoFar > worstScore) {
				for (int i = 0; i < suckerTermCount; i++) {
					if (elementLists[suckerTerms[i]]->getLastStartSmallerEq(where, &s, &e))
						if (s >= (where - DOC_LEVEL_MAX_TF)) {
							double TF = decodeDocLevelTF(s & DOC_LEVEL_MAX_TF);
							sex.score += internalWeights[suckerTerms[i]] * (k1 + 1.0) * TF / (K + TF);
						}
				}
			}
			else
				continue;
		} // end else [tf impact not cached for this doclen value]
			
	 	// we have a heap structure that contains the best "count" containers; only add
		// the current candidate to the heap if it can beat the worst extent on the heap
		if (sex.score > worstScore) {
			sex.from = start;
			sex.to = end;
			sex.containerFrom = 0;
			for (int i = 0; i < scorersInCurrentDocument; i++)
				sex.containerFrom |= (1 << whichScorer[i]);
			if (sexCount < count) {
				sexes[sexCount++] = sex;
				if (sexCount >= count) {
					sortResultsByScore(sexes, sexCount, true);
					for (int i = count; i < count * 2 + 2; i++)
						sexes[i].score = 999999.999;
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

	if ((surrogateMode != RERANK_SURROGATE_NONE) && (positionless)) {
		// perform result reranking based on the similarity of the document
		// surrogates to the surrogates of the top documents retrieved
		TerabyteSurrogates *surrogates = NULL;
		if ((surrogates == NULL) && (index->directory != NULL)) {
			int size;
			IndexCache *cache = index->getCache();
			if (cache != NULL)
				surrogates = (TerabyteSurrogates*)cache->getPointerToMiscDataFromCache("TB_SURROGATES", &size);
			if (surrogates == NULL) {
				char *fileName = evaluateRelativePathName(index->directory, "index.surrogates");
//				surrogates = new TerabyteSurrogates(fileName, false, 40, true);
				surrogates = new TerabyteSurrogates(fileName, false, 40, false);
				free(fileName);
			}
			if (cache != NULL)
				if (cache->getPointerToMiscDataFromCache("TB_SURROGATES", &size) == NULL)
					cache->addMiscDataToCache("TB_SURROGATES", surrogates, sizeof(TerabyteSurrogates), false);
		}

		if ((count > 0) && (positionless) && (index->directory != NULL)) {
			static const int MAX_RERANK_COUNT = 1000;
			static const double RHO = 1;

			TerabyteSurrogate documentSurrogates[MAX_RERANK_COUNT];
			double similarities[MAX_RERANK_COUNT];
			double maxSimilarity = 0;
			for (int i = 0; (i < MAX_RERANK_COUNT) && (i < count); i++) {
				bool status = surrogates->getSurrogate(results[i].from, &documentSurrogates[i]);
				assert(status == true);
			}
			LanguageModel *backgroundModel = index->getStaticLanguageModel();
			assert(backgroundModel->getCorpusSize() > 1);

			if (surrogateMode == RERANK_SURROGATE_COSINE) {
				// build map from term IDs to their KLD scores in the language model created
				// from the top documents
				std::map<int,double> termScores;
				for (int i = 0; (i < 10) && (i < count); i++) {
					int docLen = documentLengths[results[i].from];
					docLen <<= dlShift;
					for (int k = 0; k < documentSurrogates[i].termCount; k++) {
						int termID = documentSurrogates[i].terms[k].termID;
						double p = documentSurrogates[i].terms[k].frequency / (1.0 * docLen);
						double q = backgroundModel->getTermProbability(termID);
						if (termScores.find(termID) == termScores.end())
							termScores[termID] = 0;
						termScores[termID] += results[i].score * p * log(p / q);
					}
				}
				
				// compute the similarity between each document's keyword vector and the
				// keyword vector of the top documents
				for (int i = 0; (i < MAX_RERANK_COUNT) && (i < count); i++) {
					int docLen = documentLengths[results[i].from];
					docLen <<= dlShift;
					double score = 0;
					double normalization = 0;
					for (int k = 0; k < documentSurrogates[i].termCount; k++) {
						int termID = documentSurrogates[i].terms[k].termID;
						double p = documentSurrogates[i].terms[k].frequency / (1.0 * docLen);
						double q = backgroundModel->getTermProbability(termID);
						double termScore = p * log(p / q);
						if (termScores.find(termID) != termScores.end())
							score += termScore * termScores[termID];
						normalization += pow(termScore, 2);
					}
					similarities[i] = score / sqrt(normalization);
					if (similarities[i] > maxSimilarity)
						maxSimilarity = similarities[i];
				}
					
				// update document scores by a normalized linear combination of original score
				// and rerank score
				double maxScore = results[0].score;
				for (int i = 0; i < count; i++) {
					results[i].score = results[i].score / maxScore;
					if (i < MAX_RERANK_COUNT)
						results[i].score = RHO * similarities[i] / maxSimilarity;
				}
			} // end if (surrogateMode == RERANK_SURROGATE_COSINE)

			if (surrogateMode == RERANK_SURROGATE_KLD) {
				// build incomplete language model from surrogates of top 10 documents
				double weightSum = 0;
				std::set<int> terms;
				std::map<int,double> termFrequencies;
				std::map<int,double> votes;
				std::map<int,double>::iterator iter;
				for (int i = 0; (i < 20) && (i < count); i++)
					for (int k = 0; k < documentSurrogates[i].termCount; k++) {
						int id = documentSurrogates[i].terms[k].termID;
						terms.insert(id);
						votes[id] = termFrequencies[id] = 0;
					}
				printf("terms.size() = %d\n", static_cast<int>(terms.size()));
				for (int i = 0; (i < 10) && (i < count); i++) {
					std::set<int> termsLeft(terms);
					double curWeight = 1; //results[i].score;
					int docLen = documentLengths[results[i].from];
					docLen <<= dlShift;
					double covered = 0;
					for (int k = 0; k < documentSurrogates[i].termCount; k++) {
						int id = documentSurrogates[i].terms[k].termID;
						double p = documentSurrogates[i].terms[k].frequency / (1.0 * docLen);
						termFrequencies[id] += p * curWeight;
						votes[id] += curWeight;
char *term = backgroundModel->getTermString(id);
printf("In document %2d: \"%s\" -> %g\n", i, term, p);
free(term);
						covered += p;
						termsLeft.erase(id);
					}
					assert(covered <= 1.05);
//					for (std::set<int>::iterator it = termsLeft.begin(); it != termsLeft.end(); ++it) {
//						termFrequencies[*it] +=
//							backgroundModel->getTermProbability(*it) * (1 - covered) * curWeight;
//						votes[*it] += curWeight;
//					}
					weightSum += curWeight;
				}
				assert(weightSum > 0);

				IncompleteLanguageModel topLM(backgroundModel, false);
				for (iter = termFrequencies.begin(); iter != termFrequencies.end(); ++iter) {
					if (votes[iter->first] < 1.01)
						continue;
					double backgroundProb =
						backgroundModel->getTermProbability(iter->first);
					double p = iter->second / votes[iter->first];
					double confidence = 1 - pow(0.9, votes[iter->first]);
char *term = backgroundModel->getTermString(iter->first);
printf("%s: %g/%g (confidence: %g)\n", term, p, p * log(p / backgroundProb), confidence);
free(term);
//					; //votes[iter->first] / weightSum;
					topLM.setTermProbability(
							iter->first, backgroundProb + (p - backgroundProb) * confidence);
				}
printf("----------\n");

				// for each of the documents, compute its Kullback-Leibler divergence from
				// the language model defined by the top 10 documents, based on the incomplete
				// language model we have for both
				for (int i = 0; (i < count) && (i < MAX_RERANK_COUNT); i++) {
//					IncompleteLanguageModel docLM(backgroundModel, false);
					IncompleteLanguageModel docLM(&topLM, false);
					int docLen = documentLengths[results[i].from];
					docLen <<= dlShift;
					for (int k = 0; k < documentSurrogates[i].termCount; k++) {
						int id = documentSurrogates[i].terms[k].termID;
						double tf = documentSurrogates[i].terms[k].frequency / (1.0 * docLen);
						docLM.setTermProbability(id, tf);
					}
					results[i].score -= IncompleteLanguageModel::getKLD(&docLM, &topLM);
				}
			} // end if (surrogateMode == RERANK_SURROGATE_KLD)

			for (int i = 0; i < count; i++)
				if (results[i].score < 1)
					results[i].score = 1 / (2 - results[i].score);
			sortResultsByScore(results, count, false);
		}
	} // end if (surrogateMode != RERANK_SURROGATE_NONE)

	// If we use positionless indexing, the "from" component of each result extent
	// only contains a document number, not an actual offset. We need to translate
	// that into start/end offsets for the respective document.
	if (positionless)
		for (int i = 0; i < count; i++)
			containerList->getNth(results[i].from, &results[i].from, &results[i].to);
} // end of executeQueryDocLevel()



typedef struct {
	unsigned int id;
	float score;
} ScoredSomething;

static int compareScoredSomethings(const void *a, const void *b) {
	ScoredSomething *x = (ScoredSomething*)a;
	ScoredSomething *y = (ScoredSomething*)b;
	if (x->score > y->score)
		return -1;
	else if (y->score > x->score)
		return +1;
	else
		return 0;
} // end of compareScoredSomethings(const void*, const void*)


void TerabyteQuery::executeQueryDocLevel_TermAtATime() {
	offset start, end, s, e, containerLength;
	unsigned int containerCount = 0;
	offset avgContainerLength = 0;
	float averageContainerLength = 1.0;
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();

	IndexCache *cache = index->getCache();
	assert(cache != NULL);
	int sizeOfCachedStats;
	TerabyteCachedDocumentStatistics *cachedStats = (TerabyteCachedDocumentStatistics*)
		cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	containerCount = cachedStats->documentCount;
	averageContainerLength = cachedStats->avgDocumentLength;
	int dlShift = cachedStats->documentLengthShift;

	int sizeOfDocumentLengths;
	uint16_t *documentLengths = (uint16_t*)
		cache->getPointerToMiscDataFromCache("TB_DOCUMENT_LENGTHS", &sizeOfDocumentLengths);

	ScoredSomething sortedQueryTerms[MAX_SCORER_COUNT];
	offset matchCount = 0;

	// compute the BM25 term weight for all elements
	for (int i = 0; i < elementCount; i++) {
		elementLists[i] = elementQueries[i]->getResult();
		offset lastStart, lastEnd;
		offset listLength = elementLists[i]->getLength();
		if (listLength > 0) {
			matchCount += listLength;
			// for impact-ordered (and -restricted) lists, we encode the length of the
			// original posting list in the last element of the new list, as a delta
			// relative to DOCUMENT_COUNT_OFFSET
			if (elementLists[i]->getFirstStartBiggerEq(DOCUMENT_COUNT_OFFSET, &lastStart, &lastEnd))
				listLength = lastStart - DOCUMENT_COUNT_OFFSET;
		}
		sortedQueryTerms[i].id = i;
		sortedQueryTerms[i].score = 1.0 / (listLength + 1);
	} // end for (int i = 0; i < elementCount; i++)

	qsort(sortedQueryTerms, elementCount, sizeof(ScoredSomething), compareScoredSomethings);
	offset *matches = typed_malloc(offset, matchCount);
	matchCount = 0;
	offset preview[64];
	static const int WEIGHT_SHIFT = 20;

	for (int i = 0; i < elementCount; i++) {
		int term = sortedQueryTerms[i].id;
		start = 0;
		int n = elementLists[term]->getNextN(start, MAX_OFFSET, 64, preview, preview);
		while (n > 0) {
			int intWeight = (int)(internalWeights[term] * 10000);
			for (int k = 0; k < n; k++)
				matches[matchCount++] = (preview[k] << WEIGHT_SHIFT) + intWeight;
			start = preview[n - 1] + 1;
			n = elementLists[term]->getNextN(start, MAX_OFFSET, 64, preview, preview);
		}
	}
	sortOffsetsAscending(matches, matchCount);

	int shiftedDL;
	int documentCount = 0;
	int curDocument = -1;
	offset curScore = 0;
	for (int i = 0; i < matchCount; i++) {
		offset current = matches[i];
		int document = (current >> (WEIGHT_SHIFT + DOC_LEVEL_SHIFT));
		int tf = ((current >> WEIGHT_SHIFT) & DOC_LEVEL_MAX_TF);
		int weight = (current & ((1 << WEIGHT_SHIFT) - 1));
		if (document != curDocument) {
			if (curScore > 0)
				matches[documentCount++] = (curScore << 32) + curDocument;
			curDocument = document;
			curScore = 0;
			shiftedDL = (documentLengths[curDocument] >> dlShift);
		}
		if (shiftedDL <= MAX_CACHED_SHIFTED_DL)
			curScore += (int)(weight * cachedStats->tfImpactValue[shiftedDL][tf]);
		else {
			float K = k1 * ((1 - b) + b * documentLengths[curDocument] / averageContainerLength);
			float TF = decodeDocLevelTF(tf);
			curScore += (int)(weight * (k1 + 1.0) * TF / (K + TF));
		}
	}
	if (curScore > 0)
		matches[documentCount++] = (curScore << 32) + curDocument;
	sortOffsetsDescending(matches, documentCount);

	int count = MIN(count, documentCount);
	results = typed_malloc(ScoredExtent, count + 1);
	for (int i = 0; i < count; i++) {
		int score = (int)(matches[i] >> 32);
		int document = (int)matches[i];
		results[i].score = score / 10000.0;
		containerList->getNth(document, &results[i].from, &results[i].to);
	}
	free(matches);

} // end of executeQueryDocLevel_TermAtATime()


void TerabyteQuery::executeQueryWordLevel() {
	unsigned int containerCount = 0;
	offset avgContainerLength = 0;
	float averageContainerLength = 1.0;
	ExtentList *elementLists[MAX_SCORER_COUNT];
	ExtentList *containerList = containerQuery->getResult();

	for (int i = 0; i < elementCount; i++)
		elementLists[i] = elementQueries[i]->getResult();

	// check whether we can use cached collection statistics
	IndexCache *cache = index->getCache();
	assert(cache != NULL);
	int sizeOfCachedStats;
	TerabyteCachedDocumentStatistics *cachedStats = (TerabyteCachedDocumentStatistics*)
		cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	if (cachedStats == NULL) {
		// no cached collection statistics available yet; compute average document length
		// etc. and store the results in the index cache
		computeCollectionStats(containerList, cache);
		cachedStats = (TerabyteCachedDocumentStatistics*)
			cache->getPointerToMiscDataFromCache("TB_COLLECTION_STATS", &sizeOfCachedStats);
	}
	else if ((k1 != cachedStats->k1) || (b != cachedStats->b))
		computeCollectionStats(containerList, cache);
	assert(cachedStats != NULL);
	containerCount = cachedStats->documentCount;
	averageContainerLength = cachedStats->avgDocumentLength;

	// allocate memory and initialize everything
	int df[MAX_SCORER_COUNT], tf[MAX_SCORER_COUNT], whichScorer[MAX_SCORER_COUNT];
	float corpusWeights[MAX_SCORER_COUNT];
	float maxImpactByTerm[MAX_SCORER_COUNT];
	ScoredExtent sex;
	ScoredExtent *sexes = typed_malloc(ScoredExtent, 2 * count + 2);
	int sexCount = 0;
	offset dummy[PREVIEW + 2];

	// initialize heap structure for scorers; add sentinels at the end of the
	// heap; this will save us a couple of "if" statements; for each term, compute
	// the number of documents containing that term
	LHS **heap = typed_malloc(LHS*, elementCount * 2 + 2);
	for (int elem = 0; elem < elementCount * 2 + 2; elem++) {
		heap[elem] = typed_malloc(LHS, 1);
		heap[elem]->who = elem;
		if (elem < elementCount) {
			heap[elem]->previewPos = 0;
			heap[elem]->previewCount =
				elementLists[elem]->getNextN(0, MAX_OFFSET, PREVIEW, heap[elem]->preview, dummy);
			if (heap[elem]->previewCount > 0)
				heap[elem]->next = heap[elem]->preview[heap[elem]->previewPos++];
			else
				heap[elem]->next = MAX_OFFSET;
			df[elem] = tf[elem] = 0;
			corpusWeights[elem] = 0;
		}
		else
			heap[elem]->next = MAX_OFFSET;
	}
	qsort(heap, elementCount, sizeof(LHS*), lhsComparator);

	// compute the BM25 term weights for all elements
	LHS *heapTop = heap[0];
	while (heapTop->next < MAX_OFFSET) {
		offset curDocStart, curDocEnd;
		if (!containerList->getFirstEndBiggerEq(heap[0]->next, &curDocStart, &curDocEnd))
			break;

		while (heapTop->next <= curDocEnd) {
			int who = heapTop->who;

			while (heapTop->next < curDocStart) {
				if (heapTop->previewPos >= heapTop->previewCount) {
					heapTop->previewPos = 0;
					heapTop->previewCount = elementLists[who]->getNextN(
							heapTop->next + 1, MAX_OFFSET, PREVIEW, heapTop->preview, dummy);
					if (heapTop->previewCount <= 0) {
						heapTop->next = MAX_OFFSET;
						break;
					}
				}
				heapTop->next = heapTop->preview[heapTop->previewPos++];
			}

			if (heapTop->next <= curDocEnd) {
				corpusWeights[who] += (curDocEnd - curDocStart + 1);
				df[who]++;
			}

			while (heapTop->next <= curDocEnd) {
				if (heapTop->previewPos >= heapTop->previewCount) {
					heapTop->previewPos = 0;
					heapTop->previewCount = elementLists[who]->getNextN(
							heapTop->next + 1, MAX_OFFSET, PREVIEW, heapTop->preview, dummy);
					if (heapTop->previewCount <= 0) {
						heapTop->next = MAX_OFFSET;
						break;
					}
				}
				heapTop->next = heapTop->preview[heapTop->previewPos++];
			}

			// perform a reheap operation
			moveScorerHeapNodeDown(heap, 0);
			heapTop = heap[0];
		} // end while (heapTop->next <= curDocEnd)

	} // end while (heapTop->next < MAX_OFFSET) {

	// compute Okapi BM25 weights
	for (int i = 0; i < elementCount; i++) {
		double idfComponent = (containerCount + 0.5) / (df[i] + 0.5);
		internalWeights[i] =
			MAX(0.01, externalWeights[i] * log(MAX(idfComponent, 1.01)));
		corpusWeights[i] =
			log(corpusWeights[i] / (1.0 * elementLists[i]->getLength())) / log(2.0);

		if (!useTermProximity)
			maxImpactByTerm[i] = internalWeights[i] * (k1 + 1.0);
		else {
			// we need X+1 as a multiplier for the maximum impact here in order
			// to take term proximity scored into account
			maxImpactByTerm[i] = (internalWeights[i] + 1.0) * (k1 + 1.0);
		}

	} // end for (int i = 0; i < elementCount; i++)

	// re-initialize heap structure for the scorers
	for (int elem = 0; elem < elementCount * 2 + 2; elem++) {
		heap[elem]->who = elem;
		heap[elem]->previewPos = PREVIEW;
		heap[elem]->previewCount = PREVIEW;
		if (elem < elementCount) {
			offset dummy;
			if (!elementLists[elem]->getFirstStartBiggerEq(0, &heap[elem]->next, &dummy))
				heap[elem]->next = MAX_OFFSET;
			tf[elem] = 0;
		}
		else
			heap[elem]->next = MAX_OFFSET;
	}
	qsort(heap, elementCount, sizeof(LHS*), lhsComparator);

	// this is the lowest score on the heap; we use it as a cut-off criterion
	float worstScore = 0;

	// initialize variables for term proximity scoring
	char theseTwoAreTheSame[32][32];
	memset(theseTwoAreTheSame, 0, sizeof(theseTwoAreTheSame));
	for (int i = 0; i < 32; i++) {
		theseTwoAreTheSame[i][31] = theseTwoAreTheSame[31][i] = 1;
		theseTwoAreTheSame[i][i] = 1;
	}
	float proximityScore[32];
	for (int i = 0; i < 32; i++)
		proximityScore[i] = 0.0;

	heapTop = heap[0];
	while (heapTop->next < MAX_OFFSET) {
		offset curDocStart, curDocEnd;
		float maxImpact = 0;
		int scorersInCurrentDocument = 0;
		int previousTerm = 31;
		offset previousPosition = 0;

		if (!containerList->getFirstEndBiggerEq(heap[0]->next, &curDocStart, &curDocEnd))
			break;

		while (heapTop->next <= curDocEnd) {
			int who = heapTop->who;

			// increase TF counters; take term proximity into account
			if (heapTop->next >= curDocStart) {
				if (tf[who] == 0) {
					whichScorer[scorersInCurrentDocument++] = who;
					maxImpact += maxImpactByTerm[who];
				}
				tf[who] += 1;
				if (useTermProximity) {
					if (!theseTwoAreTheSame[previousTerm][who]) {
						float distance = (heapTop->next - previousPosition);
						if (distance < 0.5) {
							theseTwoAreTheSame[previousTerm][who] = 1;
							theseTwoAreTheSame[who][previousTerm] = 1;
						}
						else {
							static const float p = 1.5;
							static const float q = 1.5;
							proximityScore[who] +=
								p * internalWeights[previousTerm] / pow(distance, q);
							proximityScore[previousTerm] +=
								p * internalWeights[who] / pow(distance, q);
						}
					}
					previousTerm = who;
					previousPosition = heapTop->next;
				}
			} // end if (heap[0]->next >= curDocStart)

			// remove head of queue for current top element; load new data from list if necessary
			if (heapTop->previewPos < heapTop->previewCount)
				heapTop->next = heapTop->preview[heapTop->previewPos++];
			else {
				heapTop->previewPos = 0;
				heapTop->previewCount = elementLists[who]->getNextN(
						heapTop->next + 1, MAX_OFFSET, PREVIEW, heapTop->preview, dummy);
				if (heapTop->previewCount > 0)
					heapTop->next = heapTop->preview[heapTop->previewPos++];
				else
					heapTop->next = MAX_OFFSET;
			}

			// perform a reheap operation
			moveScorerHeapNodeDown(heap, 0);
			heapTop = heap[0];
		} // end while (heapTop->next <= curDocEnd)

		// check whether we can make it onto the heap; if not, go immediately to the
		// next document
		if (maxImpact <= worstScore) {
			for (int i = 0; i < scorersInCurrentDocument; i++) {
				int whichTerm = whichScorer[i];
				tf[whichTerm] = 0;
				proximityScore[whichTerm] = 0.0;
			}
			continue;
		}

		offset containerLength = (curDocEnd - curDocStart + 1);
		float K = k1 * ((1 - b) + b * containerLength / averageContainerLength);
		sex.score = 0;
		for (int i = 0; i < scorersInCurrentDocument; i++) {
			int whichTerm = whichScorer[i];
			sex.score += internalWeights[whichTerm] *
				(k1 + 1.0) * tf[whichTerm] / (K + tf[whichTerm]);
			tf[whichTerm] = 0;
			if (useTermProximity) {
				sex.score += MIN(1.0, internalWeights[whichTerm]) *
					(k1 + 1.0) * proximityScore[whichTerm] / (K + proximityScore[whichTerm]);
				proximityScore[whichTerm] = 0;
			}
		}

	 	// we have a heap structure that contains the best "count" containers; only add
		// the current candidate to the heap if it can beat the worst extent on the heap
		if (sex.score > worstScore) {
			sex.from = curDocStart;
			sex.to = curDocEnd;
			if (sexCount < count) {
				sexes[sexCount++] = sex;
				if (sexCount >= count) {
					sortResultsByScore(sexes, sexCount, true);
					for (int i = count; i < count * 2 + 2; i++)
						sexes[i].score = 999999.999;
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

	} // end while (heapTop->next < MAX_OFFSET)

	for (int elem = 0; elem < elementCount * 2 + 2; elem++)
		free(heap[elem]);
	free(heap);

	if (sexCount < count)
		count = sexCount;
	results = sexes;
	sortResultsByScore(results, count, false);
} // end of executeQueryWordLevel()


void TerabyteQuery::processModifiers(const char **modifiers) {
	BM25Query::processModifiers(modifiers);
	char *feedback = getModifierString(modifiers, "feedback", NULL);
	if (feedback != NULL) {
		if (strcasecmp(feedback, "okapi") == 0)
			pseudoRelevanceFeedback = FEEDBACK_OKAPI;
		else if (strcasecmp(feedback, "waterloo") == 0)
			pseudoRelevanceFeedback = FEEDBACK_WATERLOO;
		free(feedback);
	}
	char *rerankMode = getModifierString(modifiers, "rerank", NULL);
	surrogateMode = RERANK_SURROGATE_NONE;
	if (rerankMode != NULL) {
		if (strcasecmp(rerankMode, "surrogates_cos") == 0)
			surrogateMode = RERANK_SURROGATE_COSINE;
		else if (strcasecmp(rerankMode, "surrogates_kld") == 0)
			surrogateMode = RERANK_SURROGATE_KLD;
		free(rerankMode);
	}
} // end of processModifiers(char**)


