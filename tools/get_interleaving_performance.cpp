/**
 * This program processes a Wumpus index file (index.NNN) and outputs
 * summary information about the performance of dictionary interleaving:
 * number of in-memory dictionary entries, and average access performance
 * for first 512 bytes of postings list.
 *
 * Usage:  get_interleaving_performance INDEX_FILE BLOCK_SIZE > OUTPUT_FILE
 *
 * author: Stefan Buettcher
 * created: 2007-09-12
 * changed: 2007-09-11
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../index/compactindex.h"
#include "../misc/utils.h"


static int fd;
static const int BUFFER_SIZE = 1024 * 1024;
static char readBuffer[BUFFER_SIZE];
static int bufferSize = 0, bufferPos = 0;

static int termCount = 0, actualTermCount = 0;
static int32_t *primaryArray = NULL;
static int allocatedForPrimaryArray = 0;
static char *dictionary_as_a_string = NULL;
static int allocated_for_dictionary_as_a_string = 0;
static int used_by_dictionary_as_a_string = 0;

static int64_t indexBlockSize;


static void ensureCacheIsFull(int bytesNeeded) {
	if (bufferSize < BUFFER_SIZE)
		return;
	if (bufferPos + bytesNeeded <= bufferSize)
		return;
	bufferSize -= bufferPos;
	memmove(readBuffer, &readBuffer[bufferPos], bufferSize);
	bufferPos = 0;
	int result = read(fd, &readBuffer[bufferSize], BUFFER_SIZE - bufferSize);
	if (result > 0)
		bufferSize += result;
} // end of ensureCacheIsFull(int)


static void processIndexFile(const char *fileName) {
	fd = open(fileName, O_RDONLY);
	assert(fd >= 0);
	struct stat buf;
	int status = fstat(fd, &buf);
	assert(status == 0);

	CompactIndex_Header header;
	lseek(fd, buf.st_size - sizeof(header), SEEK_SET);
	status = read(fd, &header, sizeof(header));
	assert(status == sizeof(header));
	int listCount = header.listCount;
	lseek(fd, (off_t)0, SEEK_SET);

	bufferSize = read(fd, readBuffer, BUFFER_SIZE);
	bufferPos = 0;
	int listPos = 0;
	int64_t filePos = 0;
	char previousTerm[256];
	previousTerm[0] = 0;
	int64_t lastBlockLeader = -indexBlockSize;

	while (listPos < listCount) {
		char currentTerm[256];
		int64_t oldFilePos = filePos;
		ensureCacheIsFull(16384);
		strcpy(currentTerm, &readBuffer[bufferPos]);
		bufferPos += strlen(currentTerm) + 1;
		filePos += strlen(currentTerm) + 1;
		int32_t currentSegmentCount;
		memcpy(&currentSegmentCount, &readBuffer[bufferPos], sizeof(int32_t));
		bufferPos += sizeof(int32_t);
		filePos += sizeof(int32_t);
		PostingListSegmentHeader currentHeaders[CompactIndex::MAX_SEGMENTS_IN_MEMORY];
		memcpy(currentHeaders, &readBuffer[bufferPos],
				currentSegmentCount * sizeof(PostingListSegmentHeader));
		bufferPos += currentSegmentCount * sizeof(PostingListSegmentHeader);
		filePos += currentSegmentCount * sizeof(PostingListSegmentHeader);
		for (int k = 0; k < currentSegmentCount; k++) {
			int byteSize = currentHeaders[k].byteLength;
			ensureCacheIsFull(byteSize);
			bufferPos += byteSize;
			filePos += byteSize;
			listPos++;
		}

		actualTermCount++;
		if (oldFilePos < lastBlockLeader + indexBlockSize)
			continue;

		if (used_by_dictionary_as_a_string + 100 >= allocated_for_dictionary_as_a_string) {
			if (allocated_for_dictionary_as_a_string == 0) {
				allocated_for_dictionary_as_a_string = 1024 * 1024;
				dictionary_as_a_string =
					(char*)malloc(allocated_for_dictionary_as_a_string);
			}
			else {
				allocated_for_dictionary_as_a_string = (int)(allocated_for_dictionary_as_a_string * 1.37);
				dictionary_as_a_string =
					(char*)realloc(dictionary_as_a_string, allocated_for_dictionary_as_a_string);
			}
		}
		if (termCount >= allocatedForPrimaryArray) {
			if (allocatedForPrimaryArray == 0) {
				allocatedForPrimaryArray = 1000000;
				primaryArray = typed_malloc(int32_t, allocatedForPrimaryArray);
			}
			else {
				allocatedForPrimaryArray = (int)(allocatedForPrimaryArray * 1.37);
				primaryArray = typed_realloc(int32_t, primaryArray, allocatedForPrimaryArray);
			}
		}

		primaryArray[termCount++] = used_by_dictionary_as_a_string;
		memcpy(&dictionary_as_a_string[used_by_dictionary_as_a_string], &oldFilePos, sizeof(oldFilePos));
		used_by_dictionary_as_a_string += sizeof(oldFilePos);
		strcpy(&dictionary_as_a_string[used_by_dictionary_as_a_string], currentTerm);
		used_by_dictionary_as_a_string += strlen(currentTerm) + 1;
		lastBlockLeader = oldFilePos;
	}

	close(fd);
} // end of processIndexFile(char*)


static int64_t getFilePointerSortBased(const char *term) {
	int lower = 0, upper = termCount - 1;
	while (upper > lower) {
		int middle = (lower + upper + 1) >> 1;
		char *t = &dictionary_as_a_string[primaryArray[middle] + 8];
		if (strcmp(t, term) > 0)
			upper = middle - 1;
		else
			lower = middle;
	}
	int pos = primaryArray[lower];
	int64_t result;
	memcpy(&result, &dictionary_as_a_string[pos], sizeof(result));
	return result;
} // end of getFilePointerSortBased(char*)


int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  get_dictionary_speed INDEX_FILE BLOCK_SIZE > OUTPUT_FILE\n\n");
		return 1;
	}

	static const int RANDOM_TERM_COUNT = 5000;
	static const int ITERATIONS = 3;

	assert(sscanf(argv[2], "%lld", &indexBlockSize) == 1);

	// do measurements for sort-based dictionary
	processIndexFile(argv[1]);
	printf("Index processed. %d/%d terms added to in-memory dictionary.\n", termCount, actualTermCount);
	printf("Beginning measurements for interleaved sort-based dictionary...\n");

	// select set of random terms to use for lookup measurement later on
	char *randomTerms[RANDOM_TERM_COUNT];
	for (int i = 0; i < RANDOM_TERM_COUNT; i++) {
		randomTerms[i] = (char*)malloc(24);
		int pos = primaryArray[random() % termCount];
		char *term = &dictionary_as_a_string[pos + 8];
		strcpy(randomTerms[i], term);
	}

	char *buffer = (char*)malloc(indexBlockSize + 256);
	fd = open(argv[1], O_RDONLY);
	assert(fd >= 0);
	for (int iteration = 0; iteration < ITERATIONS; iteration++) {
		int start = currentTimeMillis();
		for (int i = 0; i < RANDOM_TERM_COUNT; i++) {
			int64_t filePointer = getFilePointerSortBased(randomTerms[i]);
			lseek(fd, filePointer, SEEK_SET);
			read(fd, buffer, indexBlockSize + 256);
		}
		int end = currentTimeMillis();
		printf("Sort-based dictionary (binary search):\n");
		printf("  Index block size: %lld bytes\n", indexBlockSize);
		printf("  Number of in-memory dictionary entries: %d\n", termCount);
		printf("  Lookup performance: %.2lf ms per term\n", (end - start) * 1.0 / RANDOM_TERM_COUNT);
		printf("  Total time: %d ms\n", end - start);
	}
	close(fd);

	return 0;
} // end of main(int, char**)


