/**
 * This program processes a Wumpus index file (index.NNN) and prints the
 * average per-term lookup latency for hash-based and sort-based dictionary
 * implementations using various hash table sizes.
 *
 * Usage:  get_dictionary_speed INDEX_FILE > OUTPUT_FILE
 *
 * author: Stefan Buettcher
 * created: 2006-12-07
 * changed: 2007-09-11
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <set>
#include <string>
#include "../index/compactindex.h"
#include "../misc/utils.h"


static const int HASH_BASED = 1;
static const int SORT_BASED = 2;


static int fd;
static const int BUFFER_SIZE = 1024 * 1024;
static char readBuffer[BUFFER_SIZE];
static int bufferSize = 0, bufferPos = 0;

static int termCount;
static int32_t *primaryArray = NULL;
static int allocatedForPrimaryArray = 0;
static char *dictionary_as_a_string = NULL;
static int allocated_for_dictionary_as_a_string = 0;
static int used_by_dictionary_as_a_string = 0;

static int32_t *hashTable = NULL;
static unsigned int hashTableSize;


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


static void processIndexFile(const char *fileName, int hashBasedOrSortBased) {
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
	int64_t listPos = 0;
	int64_t filePos = 0;
	char previousTerm[256];
	previousTerm[0] = 0;

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
		switch (hashBasedOrSortBased) {
			case HASH_BASED:
				used_by_dictionary_as_a_string += sizeof(int32_t);  // leave some space for chaining
				memcpy(&dictionary_as_a_string[used_by_dictionary_as_a_string], &oldFilePos, sizeof(oldFilePos));
				used_by_dictionary_as_a_string += sizeof(oldFilePos);
				strcpy(&dictionary_as_a_string[used_by_dictionary_as_a_string], currentTerm);
				used_by_dictionary_as_a_string += strlen(currentTerm) + 1;
				break;
			case SORT_BASED:
				memcpy(&dictionary_as_a_string[used_by_dictionary_as_a_string], &oldFilePos, sizeof(oldFilePos));
				used_by_dictionary_as_a_string += sizeof(oldFilePos);
				strcpy(&dictionary_as_a_string[used_by_dictionary_as_a_string], currentTerm);
				used_by_dictionary_as_a_string += strlen(currentTerm) + 1;
				break;
			default:
				fprintf(stderr, "Internal error!");
				exit(1);
		}
	}

	close(fd);
} // end of processIndexFile(char*, int)


static int64_t getFilePointerSortBased(const char *term) {
	int lower = 0, upper = termCount - 1;
	while (upper > lower) {
		int middle = (lower + upper) >> 1;
		char *t = &dictionary_as_a_string[primaryArray[middle] + 8];
		if (strcmp(t, term) < 0)
			lower = middle + 1;
		else
			upper = middle;
	}
	int pos = primaryArray[lower];
	assert(strcmp(term, &dictionary_as_a_string[pos + 8]) == 0);
	int64_t result;
	memcpy(&result, &dictionary_as_a_string[pos], sizeof(result));
	return result;
} // end of getFilePointerSortBased(char*)


static int64_t getFilePointerHashBased(const char *term) {
	unsigned int hashValue = simpleHashFunction(term);
	int32_t runner = hashTable[hashValue % hashTableSize];
	while (runner >= 0) {
		char *t = &dictionary_as_a_string[runner + 12];
		if (strcmp(term, t) == 0)
			break;
		runner = *((int32_t*)&dictionary_as_a_string[runner]);
	}
	assert(strcmp(term, &dictionary_as_a_string[runner + 12]) == 0);
	int64_t result;
	memcpy(&result, &dictionary_as_a_string[runner + 4], sizeof(result));
	return result;
} // end of getFilePointerHashBased(char*)


static void buildHashTable() {
	hashTable = typed_malloc(int32_t, hashTableSize);
	for (int i = 0; i < hashTableSize; i++)
		hashTable[i] = -1;
	for (int i = 0; i < termCount; i++) {
		int32_t pos = primaryArray[i];
		char *term = &dictionary_as_a_string[pos + 12];
		uint32_t slot = simpleHashFunction(term) % hashTableSize;
		*((int32_t*)&dictionary_as_a_string[pos]) = hashTable[slot];
		hashTable[slot] = pos;
	}
} // end of buildHashTable()


int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage:  get_dictionary_speed INDEX_FILE > OUTPUT_FILE\n\n");
		return 1;
	}

	static const int RANDOM_TERM_COUNT = 100000;
	static const int ITERATIONS = 3;

	// do measurements for sort-based dictionary
	processIndexFile(argv[1], SORT_BASED);
	printf("Index processed. %d terms found.\n", termCount);
	printf("Beginning measurements for sort-based dictionary...\n");

	// select set of 100000 random terms to use for lookup measurement later on
	char *randomTerms[RANDOM_TERM_COUNT];
	for (int i = 0; i < RANDOM_TERM_COUNT; i++) {
		randomTerms[i] = (char*)malloc(24);
		int pos = primaryArray[random() % termCount];
		char *term = &dictionary_as_a_string[pos + 8];
		strcpy(randomTerms[i], term);
	}

	for (int iteration = 0; iteration < ITERATIONS; iteration++) {
		int64_t sortBasedResult = 0;
		int start = currentTimeMillis(), end = start + 1;
		double searchOperations = 0;
		do {
			start = currentTimeMillis(), end = start + 1;
			searchOperations = 0;
			do {
				for (int i = 0; i < RANDOM_TERM_COUNT; i++) {
					int64_t filePointer = getFilePointerSortBased(randomTerms[i]);
					sortBasedResult ^= filePointer;
				}
				searchOperations += RANDOM_TERM_COUNT;
			} while ((end = currentTimeMillis()) < start + 10000);
		} while (end < start);
		printf("Sort-based dictionary (binary search):  --- %lld\n", sortBasedResult);
		printf("  Lookup performance: %.2lf ns per term\n", (end - start) / searchOperations * 1E6);
	}

	free(dictionary_as_a_string);
	allocated_for_dictionary_as_a_string = used_by_dictionary_as_a_string = 0;
	termCount = 0;

	// do measurements for hash-based dictionary
	processIndexFile(argv[1], HASH_BASED);
	printf("Index processed. %d terms found.\n", termCount);
	printf("Beginning measurements for hash-based dictionary...\n");

	// do measurements for hash-based dictionary
	for (int iteration = 0; iteration < 3; iteration++) {		
		for (hashTableSize = 16 * 1024 * 1024; hashTableSize >= 4 * 1024; hashTableSize /= 4) {
			int64_t hashBasedResult = 0;
			buildHashTable();
			int start = currentTimeMillis(), end = start + 1;
			double searchOperations = 0;
			do {
				start = currentTimeMillis(), end = start + 1;
				searchOperations = 0;
				do {
					for (int i = 0; i < RANDOM_TERM_COUNT; i++) {
						int64_t filePointer = getFilePointerHashBased(randomTerms[i]);
						hashBasedResult ^= filePointer;
					}
					searchOperations += RANDOM_TERM_COUNT;
				}	while ((end = currentTimeMillis()) < start + 10000);
			} while (end < start);

			printf("Hashtable size: %d  --- %lld\n", hashTableSize, hashBasedResult);
			printf("  Lookup performance: %.2lf ns per term\n", (end - start) / searchOperations * 1E6);
			free(hashTable);
		}
	}

	return 0;
} // end of main(int, char**)


