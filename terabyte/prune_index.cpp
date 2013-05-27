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
 * This application takes a document-level input index INPUT and produces an output
 * index OUTPUT. The output index contains N terms. It can be pruned by either
 * restricting the number of postings per term to a constant (K) or by specifying
 * a lower bound for the BM25 impact of every posting in the index.
 *
 * Usage:  prune_index INPUT OUTPUT [N=n] [K=k] [EPSILON=eps] [OKAPI_K1=k1] [OKAPI_B=b] [POSITIONLESS]
 *
 * author: Stefan Buettcher
 * created: 2005-10-19
 * changed: 2007-07-13
 **/


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terabyte.h"
#include "../index/compactindex.h"
#include "../index/index_types.h"
#include "../index/index_iterator.h"
#include "../index/index_merger.h"
#include "../index/multiple_index_iterator.h"
#include "../misc/all.h"
#include "../misc/logging.h"


#define BUFFER_SIZE (4 * 1024 * 1024)


static const char *LOG_ID = "CombineIndices";
static char errorMessage[256];


#define DOCUMENT_START "<doc>"
#define DOCUMENT_END "</doc>"


/** Default Okapi BM25 parameters. Can be changed through command-line options. **/
static double OKAPI_K1 = 1.2;
static double OKAPI_B = 0.50;


/** Pruning parameters. **/
static int N = 1000000;
static int K = 1000000;
static double EPSILON = 1.0;

/**
 * Tells us whether postings are actual positions or just pseudo-positions,
 * encoding document IDs.
 **/
static bool positionlessIndex = false;

/** Set to 1 via command-line switch in order to include "<doc>" and "</doc>" in output index. **/
static int INCLUDE_DOCUMENT_TAGS = 0;


/** Base for logarithmic document length scaling. **/
static const double DOC_LENGTH_BASE = 1.04;

/**
 * Minimum relative document size on a logarithmic scale. Everything smaller
 * than this is considered of size MIN_REL_SIZE.
 **/
static const int MIN_REL_SIZE = -127;

/**
 * Maximum relative document size on a logarithmic scale. Everything larger
 * than this is considered of size MAX_REL_SIZE.
 **/
static const int MAX_REL_SIZE = +127;

static char msg[256];



typedef struct {
	offset start;
	unsigned int length;
	int relativeLength;
} DocumentDescriptor;


typedef struct {
	char term[MAX_TOKEN_LENGTH + 1];
	int64_t documentCount;
} FrequentTerm;


static DocumentDescriptor *documents;
static offset documentCount;
static double avgDocLen;
static offset lastDocumentEnd;
static int prevDocumentPosition = 0;


/**
 * This array tells us for every (relDocLen,TF) pair what Okapi-style impact
 * value we have to expect.
 **/
static int impact[MAX_REL_SIZE - MIN_REL_SIZE + 4][DOC_LEVEL_MAX_TF + 1];


/**
 * This structure is used to sort postings for the same term by their relative
 * impact values.
 **/
typedef struct {
	offset posting;
	int32_t impact;
} ImpactHeapElement;


static FrequentTerm *termHeap;
static int termHeapSize;


static int compareFrequentTermsByDocumentCountAscending(const void *a, const void *b) {
	FrequentTerm *x = (FrequentTerm*)a;
	FrequentTerm *y = (FrequentTerm*)b;
	if (x->documentCount < y->documentCount)
		return -1;
	else if (x->documentCount > y->documentCount)
		return +1;
	else
		return 0;
} // end of compareFrequentTermsByDocumentCountAscending(const void*, const void*)


static int compareFrequentTermsByTermStringAscending(const void *a, const void *b) {
	FrequentTerm *x = (FrequentTerm*)a;
	FrequentTerm *y = (FrequentTerm*)b;
	return strcmp(x->term, y->term);
} // end of compareFrequentTermsByTermStringAscending(const void*, const void*)


static void collectMostFrequentTerms(IndexIterator *iterator) {

	// allocate memory for the heap that holds the "numberOfTerms" most frequent terms
	termHeapSize = 0;
	termHeap = typed_malloc(FrequentTerm, N + 1);
	for (int i = 0; i < N; i++)
		termHeap[i].documentCount = 0;

	// we use "currentTerm" to keep track of the previously processed term;
	// the assumption is that this term will not change in the current iteration,
	// but instead we will only move on to another index
	char currentTerm[MAX_TOKEN_LENGTH * 2];
	currentTerm[0] = 0;
	offset documentsForCurrentTerm = 0;

	while (iterator->hasNext()) {
		char *nextTerm = iterator->getNextTerm();

#if 0
		// drop all stop words from the final index
		char *nt = nextTerm;
		if (strncmp(nt, "<!>", 3) == 0)
			nt = &nextTerm[3];
		int ntLen = strlen(nt);
		bool dropThisTerm = false;
		if (nt[ntLen - 1] == '$') {
			nt[ntLen - 1] = 0;
			int hashSlot = simpleHashFunction(nt) % STOPWORD_HASHTABLE_SIZE;
			if (stopWordHashTable[hashSlot] > 0)
				dropThisTerm = (strcmp(nt, stopWordList[stopWordHashTable[hashSlot]]) == 0);
			nt[ntLen - 1] = '$';
		}
		else {
			int hashSlot = simpleHashFunction(nt) % STOPWORD_HASHTABLE_SIZE;
			if (stopWordHashTable[hashSlot] > 0)
				dropThisTerm = (strcmp(nt, stopWordList[stopWordHashTable[hashSlot]]) == 0);
		}
		if (dropThisTerm) {
			free(iterator->getNextListCompressed(&lengthDummy, &sizeDummy));
			continue;
		}
#endif

		if (strcmp(nextTerm, currentTerm) != 0) {
			// if the term has changed and we still have postings for the last term in
			// the output buffer, flush them to the target index
			int len = strlen(currentTerm);

			// check whether this term is one of the frequent guys, in which case we want
			// to put it onto the heap
			if ((currentTerm[0] != '<') || (currentTerm[1] == '!'))
				if ((currentTerm[3] >= 'a') || (len <= 6))
					if (documentsForCurrentTerm > 0) {
						if (termHeapSize < N) {
							strcpy(termHeap[termHeapSize].term, currentTerm);
							termHeap[termHeapSize].documentCount = documentsForCurrentTerm;
							if (++termHeapSize >= N)
								qsort(termHeap, termHeapSize, sizeof(FrequentTerm), compareFrequentTermsByDocumentCountAscending);
						}
						else if (documentsForCurrentTerm > termHeap[0].documentCount) {
							// add to existing heap if it is contained in more documents than the
							// currently least frequent term on the heap
							termHeap[0].documentCount = documentsForCurrentTerm;
							strcpy(termHeap[0].term, currentTerm);
							int parent = 0, leftChild = 1, rightChild = 2;
							while (leftChild < termHeapSize) {
								int child = leftChild;
								if (rightChild < termHeapSize)
									if (termHeap[rightChild].documentCount < termHeap[leftChild].documentCount)
										child = rightChild;
								if (termHeap[parent].documentCount <= termHeap[child].documentCount)
									break;
								FrequentTerm temp = termHeap[parent];
								termHeap[parent] = termHeap[child];
								termHeap[child] = temp;
								parent = child;
								leftChild = parent + parent + 1;
								rightChild = parent + parent + 2;
							}
						}
					} // end if (documentsForCurrentTerm > 0)

			documentsForCurrentTerm = 0;
			strcpy(currentTerm, nextTerm);
		} // end if (strcmp(nextTerm, currentTerm) != 0)

		int length, size;
		byte *compressed = iterator->getNextListCompressed(&length, &size, NULL);
		free(compressed);
		documentsForCurrentTerm += length;
	} // end while (iterator->hasNext())

	// sort list of frequent terms by ascending term string
	qsort(termHeap, termHeapSize, sizeof(FrequentTerm), compareFrequentTermsByTermStringAscending);

	offset maxDocuments = 0;
	offset minDocuments = MAX_OFFSET;
	for (int i = 0; i < termHeapSize; i++) {
		if (termHeap[i].documentCount > maxDocuments)
			maxDocuments = termHeap[i].documentCount;
		if (termHeap[i].documentCount < minDocuments)
			minDocuments = termHeap[i].documentCount;
	}

	sprintf(errorMessage, "Most frequent term appears in " OFFSET_FORMAT " documents", maxDocuments);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage, "N-th-most frequent term appears in " OFFSET_FORMAT " documents", minDocuments);
	log(LOG_DEBUG, LOG_ID, errorMessage);
} // end of mergeIndicesAndCollectMostFrequentTerms(...)


static int weirdnessCount = 0;
static offset thisOne = 0, lastOne = 0;


static int getRelDocumentLengthForPosting(offset posting) {
	// if we have a positionless index, fetching the document is easy
	if (positionlessIndex)
		return documents[posting / (DOC_LEVEL_MAX_TF + 1)].relativeLength;

	// Otherwise, do more complicated stuff...

	// Explicitly check whether we are after the last document or whether we
	// have just advanced by 1 document from the last call; in both cases,
	// return immediately.
	lastOne = thisOne;
	thisOne = posting;

	unsigned int lower, upper;
	if (documents[prevDocumentPosition + 1].start <= posting) {
		lower = prevDocumentPosition + 1;
		if (documents[lower].start + documents[lower].length > posting) {
			prevDocumentPosition = lower;
			return documents[lower].relativeLength;
		}
	}
	else
		lower = 0;
	if (posting > lastDocumentEnd)
		return MAX_REL_SIZE;

	// find subregion for binary search
	int delta = 1;
	while (lower + delta < documentCount) {
		if (documents[lower + delta].start >= posting)
			break;
		delta += delta;
	}
	upper = lower + delta;
	if (upper > documentCount)
		upper = documentCount;
	lower = lower + ((delta - 1) >> 1);

	// do a binary search in the subregion
	while (upper > lower) {
		unsigned int middle = (lower + upper + 1) >> 1;
		if (documents[middle].start > posting)
			upper = middle - 1;
		else
			lower = middle;
	}

	// print error messages in case we don't like the result; return otherwise
	if ((documents[lower].start > posting) ||
			(documents[lower].start + documents[lower].length <= posting)) {
		sprintf(errorMessage, "This should never happen: Posting outside document: "
				OFFSET_FORMAT, posting);
		log(LOG_ERROR, LOG_ID, errorMessage);
		return MAX_REL_SIZE;
	}
	else {
		if ((lower == prevDocumentPosition) && (lower > 0)) {
			sprintf(errorMessage, "WEIRD: " OFFSET_FORMAT " %d", posting, lower);
			log(LOG_ERROR, LOG_ID, errorMessage);
			weirdnessCount++;
			fprintf(stderr, "lastOne = " OFFSET_FORMAT ", thisOne = " OFFSET_FORMAT "\n", lastOne, thisOne);
			fprintf(stderr, "documentStart = " OFFSET_FORMAT "\n", documents[lower].start);
			fprintf(stderr, "documentLength = %d\n", documents[lower].length);
			fprintf(stderr, "weirdnessCount = %d\n", weirdnessCount);
			prevDocumentPosition = lower;
			return MAX_REL_SIZE;
		}
		prevDocumentPosition = lower;
		return documents[lower].relativeLength;
	}
} // end of getRelDocumentLengthForPosting(offset)


static int getImpactOfPosting(offset posting) {
	int tf = (posting & DOC_LEVEL_MAX_TF);
	int relDocLen = getRelDocumentLengthForPosting(posting);
	return impact[relDocLen - MIN_REL_SIZE][tf];
} // end of getImpactOfPosting(offset)


/** Subroutine of sortHeapByImpact. **/
static void radixSortHeap(ImpactHeapElement *inHeap, int n, int shift, ImpactHeapElement *outHeap) {
	int count[256];
	int pos[256];
	memset(count, 0, sizeof(count));
	for (int i = 0; i < n; i++) {
		int bucket = ((inHeap[i].impact >> shift) & 255);
		count[bucket]++;
	}
	pos[255] = 0;
	for (int i = 254; i >= 0; i--)
		pos[i] = pos[i + 1] + count[i + 1];
	for (int i = 0; i < n; i++) {
		int bucket = ((inHeap[i].impact >> shift) & 255);
		outHeap[pos[bucket]] = inHeap[i];
		pos[bucket]++;
	}
} // end of radixSortHeap(...)


/**
 * Sorts the given array "heap" by increasing impact. This is used to establish the
 * heap property for the array.
 **/
static void sortHeapByImpact(ImpactHeapElement *heap, int heapSize, ImpactHeapElement *temp) {
	for (int i = 0; i < sizeof(int32_t); i++) {
		if ((i & 1) == 0)
			radixSortHeap(heap, heapSize, i * 8, temp);
		else
			radixSortHeap(temp, heapSize, i * 8, heap);
	}
	for (int i = 1; i < heapSize; i++)
		assert(heap[i].impact <= heap[i - 1].impact);
} // end of sortHeapByImpact(ImpactHeapElement*, int, ImpactHeapElement*)


void radixSortPostingsByPosition(offset *inArray, int n, int shift, offset *outArray) {
	int count[256];
	int pos[256];
	memset(count, 0, sizeof(count));
	for (int i = 0; i < n; i++) {
		int bucket = ((inArray[i] >> shift) & 255);
		count[bucket]++;
	}
	pos[0] = 0;
	for (int i = 1; i < 256; i++)
		pos[i] = pos[i - 1] + count[i - 1];
	for (int i = 0; i < n; i++) {
		int bucket = ((inArray[i] >> shift) & 255);
		outArray[pos[bucket]] = inArray[i];
		pos[bucket]++;
	}
} // end of radixSortPostingsByPosition(offset*, int, int, offset*)


/**
 * Calls radixSortPostingsByPosition to perform a radix sort on the given
 * list of postings.
 **/
static void sortPostingsByPositionAscending(offset *postings, int postingCount, offset *temp) {
	for (int i = 0; i < sizeof(offset); i++) {
		if ((i & 1) == 0)
			radixSortPostingsByPosition(postings, postingCount, i * 8, temp);
		else
			radixSortPostingsByPosition(temp, postingCount, i * 8, postings);
	}
} // end of sortPostingsByPositionAscending(offset*, int, offset*)


static void addRestrictedPostingsForTerm(char *term, ExtentList *postings, CompactIndex *target) {
	if (postings->getLength() <= 1) {
		char message[256];
		sprintf(message, "Problem with term: \"%s\"\n", term);
		log(LOG_ERROR, LOG_ID, message);
	}
	assert(postings->getLength() > 1);

	offset start[256], end[256];
	offset where = 0;
	int HEAP_SIZE = K * 2;
	if (HEAP_SIZE < 10000000)
		HEAP_SIZE = 10000000;
	ImpactHeapElement *heap = typed_malloc(ImpactHeapElement, HEAP_SIZE + 4);
	ImpactHeapElement *temp = typed_malloc(ImpactHeapElement, HEAP_SIZE + 4);
	offset *postingsArray = (offset*)temp;
	offset *tempPostingsArray = (offset*)heap;
	int n, heapSize = 0;
	offset documentsForTerm = 0;

	int64_t totalImpactOfAllPostings = 0;

	// use getNextN several times to get all postings; use a heap to sort them
	// by their relative impact
	while ((n = postings->getNextN(where, MAX_OFFSET, 256, start, end)) > 0) {
		assert(start[0] >= where);
		documentsForTerm += n;
		where = start[n - 1] + 1;
		for (int i = 0; i < n; i++) {
			offset p = start[i];
			int impact = getImpactOfPosting(p);
			if (heapSize < HEAP_SIZE) {
				heap[heapSize].posting = p;
				heap[heapSize].impact = impact;
				totalImpactOfAllPostings += impact;
				if (++heapSize >= HEAP_SIZE) {
					sortHeapByImpact(heap, heapSize, temp);
					// revert order to get a MIN-HEAP
					for (int i = 0; i < heapSize/2; i++) {
						ImpactHeapElement temp = heap[i];
						heap[i] = heap[heapSize - 1 - i];
						heap[heapSize - 1 - i] = temp;
					}
				}
			}
			else {
				totalImpactOfAllPostings += impact;
				if (impact > heap[0].impact) {
					// replace the top element of the heap (posting with least impact) and
					// perform a reheap operation
					int parent = 0, leftChild = 1, rightChild = 2;
					while (leftChild < heapSize) {
						int child = leftChild;
						if (rightChild < heapSize)
							if (heap[rightChild].impact < heap[leftChild].impact)
								child = rightChild;
						if (impact <= heap[child].impact)
							break;
						heap[parent] = heap[child];
						parent = child;
						leftChild = parent + parent + 1;
						rightChild = parent + parent + 2;
					} // end while (leftChild < heapSize)
					heap[parent].posting = p;
					heap[parent].impact = impact;
				}
			}
		}
	} // end while ((n = postings->getNextN(where, MAX_OFFSET, 256, start, end)) > 0)

	assert(documentsForTerm == postings->getLength());

	// sort all postings by their impact and extract first (K,epsilon) postings
	sortHeapByImpact(heap, heapSize, temp);
	int outPos = 0;
	if (heapSize <= K) {
		for (int i = 0; i < heapSize; i++)
			postingsArray[outPos++] = heap[i].posting;
	}
	else {
		int threshold = (int)(heap[K - 1].impact * EPSILON);
		for (int i = 0; i < heapSize; i++) {
			if (heap[i].impact < threshold)
				break;
			postingsArray[outPos++] = heap[i].posting;
		}
	}

	// postings have been sorted by impact; transform to positional ordering
	sortPostingsByPositionAscending(postingsArray, outPos, tempPostingsArray);

	// finally, insert the postings into the "target" index; don't forget to add
	// a last posting that tells us the original length of the posting list (we need
	// this for computing the term weight at query time)
	postingsArray[outPos++] = DOCUMENT_COUNT_OFFSET + documentsForTerm;
	target->addPostings(term, postingsArray, outPos);

	free(heap);
	free(temp);
} // end of addRestrictedPostingsForTerm(char*, ExtentList*, CompactIndex*)


static void addPostingsForTerm(char *term, ExtentList *postings, CompactIndex *target) {
	int n;
	offset start[1024], end[1024];
	offset where = 0, total = 0;
	while ((n = postings->getNextN(where, MAX_OFFSET, 1024, start, end)) > 0) {
		target->addPostings(term, start, n);
		total += n;
		where = start[n - 1] + 1;
	}
	assert(total == postings->getLength());
} // end of addPostingsForTerm(char*, ExtentList*, CompactIndex*)


static void createOutputIndex(char *inputFile, char *outputFile) {
	CompactIndex *source = CompactIndex::getIndex(NULL, inputFile, false);
	CompactIndex *target = CompactIndex::getIndex(NULL, outputFile, true);
	IndexIterator *iterator = CompactIndex::getIterator(inputFile, BUFFER_SIZE);

	ExtentList *docStarts = source->getPostings(DOCUMENT_START);
	ExtentList *docEnds = source->getPostings(DOCUMENT_END);
	ExtentList *docs = new ExtentList_FromTo(docStarts, docEnds);

	// create "documents" array
	documentCount = docs->getLength();
	documents = typed_malloc(DocumentDescriptor, documentCount + 1);
	offset start = -1, end;
	double totalLength = 0.0;
	for (int i = 0; i < documentCount; i++) {
		docs->getFirstStartBiggerEq(start + 1, &start, &end);
		if (end > start + 1000000000)
			end = start + 1000000000;
		documents[i].start = start;
		documents[i].length = (unsigned int)(end - start + 1);
		totalLength += documents[i].length;
	}
	documents[documentCount].start = MAX_OFFSET;
	avgDocLen = totalLength / documentCount;
	delete docs;
	
	// compute relative lengths for all documents
	for (int i = 0; i < documentCount; i++) {
		double relLen = documents[i].length / avgDocLen;
		int relLenInt = LROUND(log(relLen) / log(DOC_LENGTH_BASE));
		if (relLenInt > MAX_REL_SIZE)
			relLenInt = MAX_REL_SIZE;
		else if (relLenInt < MIN_REL_SIZE)
			relLenInt = MIN_REL_SIZE;
		documents[i].relativeLength = relLenInt;
	}

	lastDocumentEnd =
		(documents[documentCount - 1].start + documents[documentCount - 1].length) - 1;

	// precompute impact values for realistic (avgDocLen, TF) pairs
	for (int docLen = MIN_REL_SIZE; docLen <= MAX_REL_SIZE; docLen++) {
		double relLen = pow(DOC_LENGTH_BASE, docLen);
		double K = OKAPI_K1 * ((1 - OKAPI_B) + OKAPI_B * relLen);
		for (int tf = 0; tf <= DOC_LEVEL_MAX_TF; tf++) {
			double TF = decodeDocLevelTF(tf);
			double impactHere = TF * (1.0 + OKAPI_K1) / (TF + K);
			impact[docLen - MIN_REL_SIZE][tf] = LROUND(impactHere * IMPACT_INTEGER_SCALING);
		}
	}

	int dc = documentCount;
	snprintf(msg, sizeof(msg), "%d documents collected. Average length is %.0lf.", dc, avgDocLen);
	log(LOG_DEBUG, LOG_ID, msg);

	// do the impact restrictions for all terms in the list; use the index iterator,
	// since our chunk-based index layout would kill us otherwise (process whole chunk
	// for each term on the heap, even if it only has a single posting)
	for (int i = 0; i < termHeapSize; i++) {
		while (iterator->hasNext()) {
			if (strcmp(iterator->getNextTerm(), termHeap[i].term) >= 0)
				break;
			iterator->skipNext();
		}
		if (!iterator->hasNext())
			break;

		offset *postings = typed_malloc(offset, termHeap[i].documentCount + 5);
		int outBufPos = 0;
		while (strcmp(iterator->getNextTerm(), termHeap[i].term) == 0) {
			int length;
			iterator->getNextListUncompressed(&length, &postings[outBufPos]);
			outBufPos += length;
			if (!iterator->hasNext())
				break;
		}

		ExtentList *list = new PostingList(postings, outBufPos, false, true);
		addRestrictedPostingsForTerm(termHeap[i].term, list, target);
		delete list;

		if (i % 10000 == 0) {
			sprintf(msg, "%d terms done.", i);
			log(LOG_DEBUG, LOG_ID, msg);
		}
	}

	free(documents);

	if (INCLUDE_DOCUMENT_TAGS) {
		ExtentList *postings = source->getPostings("</doc>");
		addPostingsForTerm("</doc>", postings, target);
		delete postings;
		postings = source->getPostings("<doc>");
		addPostingsForTerm("<doc>", postings, target);
		delete postings;
	}

	delete source;
	delete target;
} // end of createOutputIndex(char*, int)


int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage:  prune_index INPUT OUTPUT [N=n] [K=k] [EPSILON=eps] [OKAPI_K1=k1] [OKAPI_B=b] [POSITIONLESS]\n");
		return 1;
	}
	setLogLevel(LOG_DEBUG);
	log(LOG_DEBUG, LOG_ID, "Application started.");

	// process command-line options
	for (int i = 1; i < argc; i++) {
		if (strncasecmp(argv[i], "N=", 2) == 0)
			sscanf(&argv[i][2], "%d", &N);
		if (strncasecmp(argv[i], "K=", 2) == 0)
			sscanf(&argv[i][2], "%d", &K);
		if (strncasecmp(argv[i], "EPSILON=", 8) == 0)
			sscanf(&argv[i][8], "%lf", &EPSILON);
		if (strncasecmp(argv[i], "OKAPI_K1=", 9) == 0)
			sscanf(&argv[i][9], "%lf", &OKAPI_K1);
		if (strncasecmp(argv[i], "OKAPI_B=", 8) == 0)
			sscanf(&argv[i][8], "%lf", &OKAPI_B);
		if (strcasecmp(argv[i], "POSITIONLESS") == 0)
			positionlessIndex = true;
		if (strncasecmp(argv[i], "INCLUDE_DOCUMENT_TAGS=", 22) == 0)
			sscanf(&argv[i][22], "%d", &INCLUDE_DOCUMENT_TAGS);
	}

	sprintf(msg, "N=%d, K=%d, EPSILON=%.2lf, OKAPI_K1=%.2lf, OKAPI_B=%.2lf",
			N, K, EPSILON, OKAPI_K1, OKAPI_B);
	log(LOG_DEBUG, LOG_ID, msg);
	sprintf(msg, "INCLUDE_DOCUMENT_TAGS=%d", INCLUDE_DOCUMENT_TAGS);
	log(LOG_DEBUG, LOG_ID, msg);

	// check whether filenames are ok
	struct stat buf;
	char *inputFile = argv[1];
	if (stat(inputFile, &buf) != 0) {
		fprintf(stderr, "Error: Input index \"%s\" does not exist.\n", inputFile);
		return 1;
	}
	char *outputFile = argv[2];
	if (stat(outputFile, &buf) == 0) {
		fprintf(stderr, "Error: Output index \"%s\" already exists.\n", outputFile);
		return 1;
	}

	// adjusting parameters
	if (N < 1) {
		log(LOG_ERROR, LOG_ID, "Setting N := 1.");
		N = 1;
	}
	if (N > 10000000) {
		log(LOG_ERROR, LOG_ID, "Setting N := 10000000.");
		N = 10000000;
	}
	if (K < 1) {
		log(LOG_ERROR, LOG_ID, "Setting K := 1.");
		K = 1;
	}
	if (K > 10000000) {
		log(LOG_ERROR, LOG_ID, "Setting K := 10000000.");
		K = 10000000;
	}

	IndexIterator *iterator = CompactIndex::getIterator(inputFile, BUFFER_SIZE);
	initializeStopWordHashtable();
	collectMostFrequentTerms(iterator);
	delete iterator;

	log(LOG_DEBUG, LOG_ID, "Indices merged and frequent terms collected.");
	sprintf(errorMessage, "Sorting postings for %d most frequent terms by their impact...", termHeapSize);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	createOutputIndex(inputFile, outputFile);
	
	return 0;
} // end of main(int, char**)


