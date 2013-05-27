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
bool preserveTF = false, onlyTF = false;
CompactIndex *outputIndex;
long long totalSizeOfPostings = 0;

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
#if 1
		offset docid = (postings[i] >> DOC_LEVEL_SHIFT);
		offset tf = (postings[i] & DOC_LEVEL_MAX_TF);
#else
		offset docid = postings[i], tf = 1;
#endif
		assert(docid >= 0);
		if (docid >= documentCount) {
			int newDocCnt = docid + 1000;
			newOrdering = (int*)realloc(newOrdering, newDocCnt * sizeof(int));
			for (int k = documentCount; k < newDocCnt; k++)
				newOrdering[k] = k;
			documentCount = newDocCnt;
		}
		docid = newOrdering[docid];
		if ((preserveTF) || (onlyTF))
			postings[i] = (docid << DOC_LEVEL_SHIFT) + tf;
		else
			postings[i] = docid;
	}
	assert(pCnt > 0);
	sortOffsetsAscending(postings, pCnt);

	if (onlyTF) {
		postings[0] = (postings[0] & DOC_LEVEL_MAX_TF);
		for (int i = 1; i < pCnt; i++) {
			offset tf = decodeDocLevelTF(postings[i] & DOC_LEVEL_MAX_TF);
			postings[i] = postings[i - 1] + tf;
		}
	}

	if (pCnt < MAX_SEGMENT_SIZE) {
		int byteLength;
		byte *compressed = c(postings, pCnt, &byteLength);
		totalSizeOfPostings += byteLength;
		outputIndex->addPostings(
				term, compressed, byteLength, pCnt, postings[0], postings[pCnt - 1]);
		free(compressed);
	}
	else {
		int left = pCnt;
		while (left > 0) {
			int cnt = (left < MAX_SEGMENT_SIZE ? left : MIN_SEGMENT_SIZE);
			int byteLength;
			byte *compressed = c(&postings[pCnt - left], cnt, &byteLength);
			totalSizeOfPostings += byteLength;
			outputIndex->addPostings(
					term, compressed, byteLength, cnt, postings[pCnt - left], postings[pCnt - left + cnt - 1]);
			left -= cnt;
			free(compressed);
		}
	}
}


int main(int argc, char **argv) {
	if (argc != 6) {
		fprintf(stderr, "Usage:  build_reordered_index INPUT_INDEX OUTPUT_INDEX COMPRESSION_METHOD REORDER_MATRIX --WITH_TF|--WITHOUT_TF|--ONLY_TF\n\n");
		fprintf(stderr, "INPUT_INDEX OUTPUT_INDEX are inverted indices. REORDER_MATRIX is the output\n");
		fprintf(stderr, "of the build_reorder_matrix application.\n\n");
		return 1;
	}

	processReorderMatrix(argv[4]);
	preserveTF = (strcasecmp(argv[5], "--WITH_TF") == 0);
	onlyTF = (strcasecmp(argv[5], "--ONLY_TF") == 0);
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

	printf("Combined size of all compressed posting lists: %lld bytes.\n", totalSizeOfPostings);
	return 0;
}


