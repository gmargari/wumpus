/**
 * Copyright (C) 2005 Stefan Buettcher. All rights reserved.
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
 * This program takes a document-level index instance and prunes all postings
 * lists according to Fagin's (k, epsilon) method.
 *
 * author: Stefan Buettcher
 * created: 2008-09-10
 * changed: 2008-09-10
 **/


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../index/index.h"
#include "../index/index_types.h"
#include "../index/compactindex.h"
#include "../index/index_iterator.h"
#include "../index/multiple_index_iterator.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"
#include "../terabyte/terabyte.h"


/** Default Okapi BM25 parameters. **/
static const double OKAPI_K1 = 1.2;
static const double OKAPI_B = 0.75;

// Total number of documents in the index.
int documentCount;

// Lengths of all documents, in tokens.
int *docLens;

// Average document length, in tokens.
double avgDocLen;

// Pruning parameters.
int k;
double epsilon;


void printSyntax() {
	printf("Syntax:   restrict_by_impact OLD_INDEX NEW_INDEX K EPSILON\n\n");
	printf("Restricts the documents for every term in the document-level index\n");
	printf("according to Fagin's (k, epsilon) pruning method.\n\n");
	exit(1);
}


void processPostings(const char *term, offset *postings, int pCnt, CompactIndex *outputIndex) {
	fprintf(stderr, "Processing %d postings for term: %s\n", pCnt, term);

	if (pCnt == 0) {
	}
	else if (pCnt <= k) {
		postings[pCnt] = DOCUMENT_COUNT_OFFSET + pCnt;
		outputIndex->addPostings(term, postings, pCnt + 1);
	}
	else {
		int impactCounts[5000];
		memset(impactCounts, 0, sizeof(impactCounts));

		double *impacts = typed_malloc(double, pCnt);
		for (int i = 0; i < pCnt; ++i) {
			int docid = (postings[i] >> DOC_LEVEL_SHIFT);
			int tf = decodeDocLevelTF(postings[i] & DOC_LEVEL_MAX_TF);
			double dl = docLens[docid];
			impacts[i] = (OKAPI_K1 + 1.0) * tf / (tf + OKAPI_K1 * (1 - OKAPI_B + OKAPI_B * dl / avgDocLen));
			impactCounts[lround(impacts[i] * 1000)]++;
		}

		double threshold = 0.0;
		int sum = 0;
		for (int i = 4999; i >= 0; --i) {
			sum += impactCounts[i];
			if (sum >= k) {
				threshold = epsilon * (i / 1000.0);
				break;
			}
		}

		int newPCnt = 0;
		for (int i = 0; i < pCnt; ++i)
			if (impacts[i] >= threshold)
				postings[newPCnt++] = postings[i];
		postings[newPCnt++] = DOCUMENT_COUNT_OFFSET + pCnt;

		outputIndex->addPostings(term, postings, newPCnt);
		delete impacts;
	}
}


void pruneIndex(const char *inputFile, const char *outputFile) {
	assert(fileExists(inputFile));
	assert(!fileExists(outputFile));

	// Open input file and create output file.
	CompactIndex *inputIndex = CompactIndex::getIndex(NULL, inputFile, false);
	IndexIterator *inputIterator = CompactIndex::getIterator(inputFile, 4 << 20);
	CompactIndex *outputIndex = CompactIndex::getIndex(NULL, outputFile, true);

	// Obtain all documents lengths and compute avgDocLen.
	ExtentList *startDoc = inputIndex->getPostings("<doc>");
	ExtentList *endDoc = inputIndex->getPostings("</doc>");
	ExtentList_FromTo documents(startDoc, endDoc);
	documentCount = documents.getLength();
	docLens = typed_malloc(int, documentCount);
	offset start = -1, end;
	avgDocLen = 0.0;
	for (int i = 0; i < documentCount; ++i) {
		assert(documents.getFirstStartBiggerEq(start + 1, &start, &end));
		docLens[i] = start - end + 1;
		avgDocLen += docLens[i];
	}
	avgDocLen /= documentCount;
	assert(!documents.getFirstStartBiggerEq(start + 1, &start, &end));

	offset *postings = typed_malloc(offset, documentCount + 65536);
	int pCnt = 0;
	char currentTerm[256] = { 0 };
	while (inputIterator->hasNext()) {
		const char *nextTerm = inputIterator->getNextTerm();
		assert(nextTerm != NULL);
		if (strcmp(inputIterator->getNextTerm(), currentTerm) != 0) {
			processPostings(currentTerm, postings, pCnt, outputIndex);
			strcpy(currentTerm, nextTerm);
			pCnt = 0;
		}
		if ((strcmp(currentTerm, "<doc>") == 0) || (strcmp(currentTerm, "</doc>") == 0)) {
			inputIterator->getNextListUncompressed(&pCnt, postings);
			outputIndex->addPostings(currentTerm, postings, pCnt);
			pCnt = 0;
		}
		else if (strncmp(currentTerm, "<!>", 3) == 0) {
			int length;
			inputIterator->getNextListUncompressed(&length, postings + pCnt);
			pCnt += length;
			assert(pCnt <= documentCount);
		}
		else {
			inputIterator->skipNext();
		}
	}
	processPostings(currentTerm, postings, pCnt, outputIndex);

	fprintf(stderr, "Done. Finalizing output index.\n");
	delete outputIndex;
}


int main(int argc, char **argv) {
	initializeConfigurator(NULL, NULL);

	if (argc != 5)
		printSyntax();
	assert(fileExists(argv[1]));
	assert(!fileExists(argv[2]));
	assert(1 == sscanf(argv[3], "%d", &k));
	assert(k > 0);
	assert(1 == sscanf(argv[4], "%lf", &epsilon));
	assert((epsilon >= 0.0) && (epsilon <= 1.0));
	pruneIndex(argv[1], argv[2]);
	return 0;
} // end of main(int, char**)



