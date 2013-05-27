#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../index/index_compression.h"
#include "../../index/compactindex.h"
#include "../../index/index_iterator.h"
#include "../../terabyte/terabyte.h"


int documentCount = 0;
int *newOrdering = NULL;
offset *postings = NULL;
int pCnt = 0;
bool preserveTF = false;
CompactIndex *outputIndex;

static const int MAX_POSTINGS = 26000000;


void processReorderMatrix(char *fileName) {
	FILE *f = fopen(fileName, "r");
	assert(f != NULL);
	char line[256];
	documentCount = -1;
	while (true) {
		fgets(line, sizeof(line), f);
		if (line[0] == '#')
			continue;
		if (strncmp(line, "DOCUMENT_COUNT: ", 16) == 0) {
			sscanf(&line[16], "%d", &documentCount);
			break;
		}
	}
	fprintf(stderr, "Reading reordering matrix with %d elements.\n", documentCount);
	assert(documentCount > 0);
	newOrdering = (int*)malloc(documentCount * sizeof(int));
	for (int i = 0; i < documentCount; i++) {
		char *p = fgets(line, sizeof(line), f);
		assert(p != NULL);
		int oldID, newID;
		int status = sscanf(line, "%d%d", &oldID, &newID);
		assert(status == 2);
		assert(oldID == i);
		newOrdering[i] = newID;
	}
	fclose(f);
}


void addReorderedPostings(char *term, Compressor c) {
	if ((strncmp(term, "<doc", 4) == 0) || (strncmp(term, "</doc", 5) == 0))
		return;

	for (int i = 0; i < pCnt; i++) {
		if (postings[i] >= DOCUMENT_COUNT_OFFSET) {
			pCnt = i;
			break;
		}
		offset docid = (postings[i] >> DOC_LEVEL_SHIFT);
		offset tf = (postings[i] & DOC_LEVEL_MAX_TF);
		assert(docid >= 0);
		if (docid >= documentCount) {
			int newDocCnt = docid + 1000;
			newOrdering = (int*)realloc(newOrdering, newDocCnt * sizeof(int));
			for (int k = documentCount; k < newDocCnt; k++)
				newOrdering[k] = k;
			documentCount = newDocCnt;
		}
		docid = newOrdering[docid];
		if (preserveTF)
			postings[i] = (docid << DOC_LEVEL_SHIFT) + tf;
		else
			postings[i] = docid;
	}
	assert(pCnt > 0);
	sortOffsetsAscending(postings, pCnt);

	if (pCnt < MAX_SEGMENT_SIZE) {
		int byteLength;
		byte *compressed = c(postings, pCnt, &byteLength);
		outputIndex->addPostings(
				term, compressed, byteLength, pCnt, postings[0], postings[pCnt - 1]);
		free(compressed);
	}
	else {
		int chunkCnt = (pCnt / TARGET_SEGMENT_SIZE) + 1;
		int chunkSize = pCnt / chunkCnt + 1;
		int left = pCnt;
		for (int i = 0; i < chunkCnt; i++) {
			int cnt = (chunkSize < left ? chunkSize : left);
			int byteLength;
			byte *compressed = c(&postings[pCnt - left], cnt, &byteLength);
			outputIndex->addPostings(
					term, compressed, byteLength, cnt, postings[pCnt - left], postings[pCnt - left + cnt - 1]);
			left -= cnt;
			free(compressed);
		}
	}
}


int main(int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "Usage:  build_reordered_si_index INPUT_INDEX OUTPUT_INDEX COMPRESSION_METHOD REORDER_MATRIX\n\n");
		fprintf(stderr, "INPUT_INDEX OUTPUT_INDEX are inverted indices. REORDER_MATRIX is the output\n");
		fprintf(stderr, "of the build_reorder_matrix application.\n\n");
		return 1;
	}

	processReorderMatrix(argv[4]);
	Compressor compressor = compressorForID[getCompressorForName(argv[3])];
	
	IndexIterator iter(argv[1], 1024 * 1024);
	outputIndex = CompactIndex::getIndex(NULL, argv[2], true);
	postings = (offset*)malloc(MAX_POSTINGS * sizeof(offset));
	char curTerm[32];
	curTerm[0] = 0;
	while (iter.hasNext()) {
		char nextTerm[32];
		strcpy(nextTerm, iter.getNextTerm());
		if (strcmp(curTerm, nextTerm) != 0) {
			if (pCnt > 0) {
				addReorderedPostings(curTerm, compressor);
				pCnt = 0;
			}
			strcpy(curTerm, nextTerm);
		}
		int cnt;
		iter.getNextListUncompressed(&cnt, &postings[pCnt]);
		pCnt += cnt;
	}
	if (pCnt > 0)
		addReorderedPostings(curTerm, compressor);
	delete outputIndex;

	return 0;
}


