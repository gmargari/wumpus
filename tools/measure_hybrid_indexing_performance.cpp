/**
 * This program's purpose is to examine the benefits of hybrid index
 * construction, where some terms' lists are indexed according to hash-based
 * index construction, while others are kept in a pool that is re-ordered
 * using Radixsort whenever the indexing process runs out of memory.
 *
 * author: Stefan Buettcher
 * created: 2007-09-27
 * changed: 2007-09-27
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../filters/trec_inputstream.h"
#include "../index/compactindex.h"
#include "../index/index_iterator.h"
#include "../index/index_merger.h"
#include "../index/multiple_index_iterator.h"
#include "../misc/utils.h"


#define EXTERNAL_TERM_STRINGS 0


static const unsigned int CONTAINER_SHIFT = 19;
static const unsigned int CONTAINER_SIZE = (1 << CONTAINER_SHIFT);
static const unsigned int MAX_CONTAINER_COUNT = (1 << 13);

static char *termContainers[MAX_CONTAINER_COUNT];
static uint32_t termContainerCount = 0, termContainerPos = 0;

static char *poolContainers[MAX_CONTAINER_COUNT];
static uint32_t poolContainerCount = 0, poolContainerPos = 0;
static uint64_t *poolPointer = NULL;

static char *listContainers[MAX_CONTAINER_COUNT];
static uint32_t listContainerCount = 0, listContainerPos = 0;

static uint32_t POSTINGS_THRESHOLD = 8;
static const uint32_t POSTINGS_GROUP_SIZE = 8;

static const uint32_t TERMID_SHIFT = 32;

static const uint32_t HASHTABLE_SIZE = 65536;
static int32_t hashTable[HASHTABLE_SIZE];

static uint32_t memoryLimit = 0;
static uint32_t subIndexCount = 0;


struct DictionaryEntry {
	uint32_t hashValue;     // 32-bit hash value; union with term ID
	int32_t next;           // next term in collision chain
	int32_t pCnt;           // number of postings for this term
	char termString[20];
	int32_t *firstPosting;  // pointer to first posting, if hash-indexed term
	int32_t *lastPosting;   // pointer to last posting, if hash-indexed term
}; // end of struct DictionaryEntry


static DictionaryEntry *dictionary = NULL;
static unsigned int dictionarySlots, termCount;


static int compareLex(const void *a, const void *b) {
#if EXTERNAL_TERM_STRINGS
	uint32_t x = ((DictionaryEntry*)a)->termString;
	uint32_t y = ((DictionaryEntry*)b)->termString;
	char *xString = &termContainers[x >> CONTAINER_SHIFT][x & (CONTAINER_SIZE - 1)];
	char *yString = &termContainers[y >> CONTAINER_SHIFT][y & (CONTAINER_SIZE - 1)];
	return strcmp(xString, yString);
#else
	DictionaryEntry *x = (DictionaryEntry*)a;
	DictionaryEntry *y = (DictionaryEntry*)b;
	return strcmp(x->termString, y->termString);
#endif
} // end of compareLex(const void *a, const void *b)


static void reset() {
	memset(hashTable, 255, sizeof(hashTable));
	if (dictionary == NULL) {
		dictionarySlots = 65536;
		dictionary = typed_malloc(DictionaryEntry, dictionarySlots);
	}
	termCount = 0;

	for (int i = 0; i < termContainerCount; i++)
		free(termContainers[i]);
	termContainerCount = 0;
	termContainerPos = CONTAINER_SIZE;

	for (int i = 0; i < listContainerCount; i++)
		free(listContainers[i]);
	listContainerCount = 0;
	listContainerPos = CONTAINER_SIZE;

	for (int i = 0; i < poolContainerCount; i++)
		free(poolContainers[i]);
	poolContainerCount = 0;
	poolContainerPos = CONTAINER_SIZE;
	poolPointer = NULL;
} // end of reset()


static void buildIndexPartition() {
	static const int RADIXSORT_BIT_COUNT = 6;
	static const int RADIXSORT_BUCKET_COUNT = (1 << RADIXSORT_BIT_COUNT);
	static const int RADIXSORT_MAX_BUCKET = RADIXSORT_BUCKET_COUNT - 1;
	static const int RADIXSORT_ITERATIONS = 4;

	printf("  term bytes: %d\n", termCount * sizeof(DictionaryEntry));
	printf("  list bytes: %d\n", (listContainerCount - 1) * CONTAINER_SIZE + listContainerPos);
	printf("  pool bytes: %d\n", (poolContainerCount - 1) * CONTAINER_SIZE + poolContainerPos);

	// sort terms in lexicographical order; adjust term IDs based on new ordering
	// and collect counts for radixsort
	uint32_t poolSize = ((poolContainerCount - 1) * CONTAINER_SIZE + poolContainerPos) / 8;
	uint64_t *array1 = typed_malloc(uint64_t, poolSize + 1);
	uint64_t *array2 = typed_malloc(uint64_t, poolSize + 1);
	for (int i = 0; i < termCount; i++)
		dictionary[i].hashValue = i;
	qsort(dictionary, termCount, sizeof(DictionaryEntry), compareLex);

	uint32_t *idMap = typed_malloc(uint32_t, termCount + 1);
	for (int i = 0; i < termCount; i++)
		idMap[dictionary[i].hashValue] = i;

	uint32_t cnt[RADIXSORT_ITERATIONS][RADIXSORT_BUCKET_COUNT];
	memset(cnt, 0, sizeof(cnt));
	uint32_t outPos = 0;
	for (int i = 0; i < poolContainerCount; i++) {
		uint64_t *array = (uint64_t*)poolContainers[i];
		uint32_t limit = (i == poolContainerCount - 1 ? poolContainerPos / 8 : CONTAINER_SIZE / 8);
		for (uint32_t k = 0; k < limit; k++) {
			uint64_t value = array[k];
			uint64_t termID = idMap[value >> 32];
			array1[outPos++] = (value & 0xFFFFFFFF) | (termID << 32);
			for (int j = 0; j < RADIXSORT_ITERATIONS; j++) {
				cnt[j][termID & RADIXSORT_MAX_BUCKET]++;
				termID >>= RADIXSORT_BIT_COUNT;
			}
		}
	}
	free(idMap);

	// compute start positions of output chunks, from statistics gathered
	for (int i = 0; i < RADIXSORT_ITERATIONS; i++) {
		uint32_t *c = cnt[i];
		c[RADIXSORT_MAX_BUCKET] = poolSize - c[RADIXSORT_MAX_BUCKET];
		for (int k = RADIXSORT_MAX_BUCKET - 1; k >= 0; k--)
			c[k] = c[k + 1] - c[k];
	}

	// execute radixsort on postings in pool
	for (int i = 0; i < RADIXSORT_ITERATIONS; i++) {
		uint32_t *c = cnt[i];
		uint32_t shift = TERMID_SHIFT + i * RADIXSORT_BIT_COUNT;
		for (uint32_t k = 0; k < poolSize; k++) {
			uint64_t value = array1[k];
			uint32_t bucket = ((value >> shift) & RADIXSORT_MAX_BUCKET);
			array2[c[bucket]++] = value;
		}
		uint64_t *tmp = array1;
		array1 = array2;
		array2 = tmp;
	}
	free(array2);
	uint64_t *pool = array1;

	// send all postings to the output index
	char fileName[256];
	sprintf(fileName, "index.%04d", subIndexCount++);
	CompactIndex *index = CompactIndex::getIndex(NULL, fileName, true);

	byte *compressed = (byte*)malloc(8 * MAX_SEGMENT_SIZE);
	compressed[0] = COMPRESSION_VBYTE;
	for (int i = 0; i < termCount; i++) {
		// setup compressed postings header
		int pCnt = dictionary[i].pCnt;
		int outPos = 1, left = pCnt, inThisBatch = 0;
		if (pCnt <= MAX_SEGMENT_SIZE)
			outPos += encodeVByte32(pCnt, &compressed[1]);
		else
			outPos += encodeVByte32(MIN_SEGMENT_SIZE, &compressed[1]);

		// first, collect postings from pool
		int poolCnt = MIN(pCnt, POSTINGS_THRESHOLD - 1);
		int32_t *chunk = dictionary[i].firstPosting;
		int32_t firstInBatch = (POSTINGS_THRESHOLD == 1 ? chunk[1] : pool[0] & 0xFFFFFFFF);
		int32_t prev = 0;
		for (int k = 0; k < poolCnt; k++) {
			int32_t posting = (pool[k] & 0xFFFFFFFF);
			int32_t delta = posting - prev;
			while (delta >= 128) {
				compressed[outPos++] = (delta & 127);
				delta >>= 7;
			}
			compressed[outPos++] = delta;
			prev = posting;
		}
		pool += poolCnt;
		inThisBatch += poolCnt;
		left -= poolCnt;

		// then, collect postings from term-specific list
		while (chunk != NULL) {
			int32_t chunkHeader = chunk[0];
			if (chunkHeader < 0) {
				for (int k = 1; k < POSTINGS_GROUP_SIZE; k++) {
					int32_t posting = (chunk[k] & 0xFFFFFFFF);
					int32_t delta = posting - prev;
					while (delta > 0) {
						compressed[outPos++] = (delta & 127);
						delta >>= 7;
					}
					prev = posting;
				}
				inThisBatch += POSTINGS_GROUP_SIZE - 1;
				left -= POSTINGS_GROUP_SIZE - 1;
				int container = ((-chunkHeader) >> CONTAINER_SHIFT);
				int pos = (-chunkHeader) & (CONTAINER_SIZE - 1);
				chunk = (int32_t*)&listContainers[container][pos];
			}
			else {
				for (int k = 1; k < chunkHeader; k++) {
					int32_t posting = (chunk[k] & 0xFFFFFFFF);
					int32_t delta = posting - prev;
					while (delta > 0) {
						compressed[outPos++] = (delta & 127);
						delta >>= 7;
					}
					prev = posting;
				}
				inThisBatch += chunkHeader - 1;
				left -= chunkHeader - 1;
				chunk = NULL;
			}

			if ((inThisBatch > MIN_SEGMENT_SIZE) && (inThisBatch + left > MAX_SEGMENT_SIZE)) {
				encodeVByte32(inThisBatch, &compressed[1]);
				index->addPostings(dictionary[i].termString,
						compressed, outPos, inThisBatch, firstInBatch, prev);
				assert(chunk != NULL);
				firstInBatch = chunk[1];
				prev = 0;
				inThisBatch = 0;
				outPos = 1 + encodeVByte32(MIN_SEGMENT_SIZE, &compressed[1]);
			}
		} // end while (chunk != NULL)

		encodeVByte32(inThisBatch, &compressed[1]);
		index->addPostings(dictionary[i].termString,
				compressed, outPos, inThisBatch, firstInBatch, prev);		
	} // end for (int i = 0; i < termCount; i++)

	delete index;
	free(compressed);
	free(array1);
} // end of buildIndexPartition()


static void buildIndex() {
	int last = 0;
	reset();

	int myMemoryLimit = memoryLimit - CompactIndex::WRITE_CACHE_SIZE;
	int memoryConsumption = 0;

	InputToken token;
	TRECInputStream tokenizer(fileno(stdin));
	while (tokenizer.getNextToken(&token)) {

		// make sure there is sufficient space in all container types
		if (termContainerPos > CONTAINER_SIZE - 32) {
			termContainers[termContainerCount++] = (char*)malloc(CONTAINER_SIZE);
			termContainerPos = 0;
			memoryConsumption += CONTAINER_SIZE;
		}
		if (listContainerPos > CONTAINER_SIZE - POSTINGS_GROUP_SIZE * sizeof(uint32_t)) {
			listContainers[listContainerCount++] = (char*)malloc(CONTAINER_SIZE);
			listContainerPos = 0;
			memoryConsumption += CONTAINER_SIZE;
		}
		if (poolContainerPos > CONTAINER_SIZE - 8) {
			poolContainers[poolContainerCount++] = (char*)malloc(CONTAINER_SIZE);
			poolPointer = (uint64_t*)poolContainers[poolContainerCount - 1];
			poolContainerPos = 0;
			memoryConsumption += CONTAINER_SIZE;
		}

		// lookup term descriptor in dictionary
		char *term = (char*)token.token;
		uint32_t hashValue = simpleHashFunction(term);
		uint32_t hashSlot = hashValue % HASHTABLE_SIZE;
		int32_t runner = hashTable[hashSlot], prev = -1;
		while (runner >= 0) {
			if (dictionary[runner].hashValue == hashValue) {
#if EXTERNAL_TERM_STRINGS
				uint32_t s = dictionary[runner].termString;
				if (strcmp(&termContainers[s >> CONTAINER_SHIFT][s & (CONTAINER_SIZE - 1)], term) == 0)
					break;
#else
				if (strcmp(dictionary[runner].termString, term) == 0)
					break;
#endif
			}
			prev = runner;
			runner = dictionary[runner].next;
		}

		DictionaryEntry *de = &dictionary[runner];
		if (runner < 0) {
			// term not found: create new dictionary entry
			if (termCount >= dictionarySlots) {
				memoryConsumption += dictionarySlots * sizeof(DictionaryEntry);
				dictionarySlots *= 2;
				dictionary = typed_realloc(DictionaryEntry, dictionary, dictionarySlots);
			}
			de = &dictionary[termCount];
			de->hashValue = hashValue;
			de->next = -1;
			de->pCnt = 0;
			de->firstPosting = NULL;
#if EXTERNAL_TERM_STRINGS
			int len = strlen(term);
			de->termString = ((termContainerCount - 1) << CONTAINER_SHIFT) + termContainerPos;
			memcpy(&termContainers[termContainerCount - 1][termContainerPos], term, len + 1);
			termContainerPos += len + 1;
#else
			strcpy(de->termString, term);
#endif
			if (prev < 0)
				hashTable[hashSlot] = termCount;
			else
				dictionary[prev].next = termCount;
			runner = termCount++;
		}
		else if (prev >= 0) {
			// term found: move term descriptor to front of collision chain
			dictionary[prev].next = de->next;
			de->next = hashTable[hashSlot];
			hashTable[hashSlot] = runner;
		}

		int32_t posting = token.sequenceNumber;
		if (++de->pCnt >= POSTINGS_THRESHOLD) {
			if (de->pCnt == POSTINGS_THRESHOLD) {
				de->firstPosting =
					(int32_t*)&listContainers[listContainerCount - 1][listContainerPos];
				de->lastPosting =
					de->firstPosting;
				de->lastPosting[0] = 2;
				de->lastPosting[1] = posting;
				listContainerPos += 4 * POSTINGS_GROUP_SIZE;
			}
			else {
				int32_t *lp = de->lastPosting;
				if (lp[0] >= POSTINGS_GROUP_SIZE) {
					lp[0] = -(((listContainerCount - 1) << CONTAINER_SHIFT) + listContainerPos);
					lp = de->lastPosting = (int32_t*)&listContainers[listContainerCount - 1][listContainerPos];
					lp[0] = 1;
					listContainerPos += 4 * POSTINGS_GROUP_SIZE;
				}
				lp[lp[0]++] = posting;
			}
		}
		else {
			uint64_t value = runner;
			*(poolPointer++) = (value << 32) | posting;
			poolContainerPos += 8;
		}

		// check whether we have reached the memory limit; if so, transfer all
		// postings to disk
		if (memoryConsumption > myMemoryLimit) {
			printf("building index partition for %d terms with %d postings\n",
					termCount, posting - last);
			last = posting;
			buildIndexPartition();
			reset();
			memoryConsumption = 0;
		}

	} // end while (tokenizer.getNextToken(&token))

	printf("building index partition for %d terms with ??? postings\n", termCount);
	buildIndexPartition();
} // end of buildIndex()


int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  measure_hybrid_indexing_performance POSTINGS_THRESHOLD MEMORY_LIMIT < INPUT_DATA\n\n");
		return 1;
	}
	int status = sscanf(argv[2], "%d", &memoryLimit);
	assert(status == 1);
	assert(memoryLimit > 2 * CompactIndex::WRITE_CACHE_SIZE);
	status = sscanf(argv[1], "%d", &POSTINGS_THRESHOLD);
	assert(status == 1);
	assert(POSTINGS_THRESHOLD > 0);

	printf("Indexing with %d bytes of memory and a threshold of %d postings.\n",
			memoryLimit, POSTINGS_THRESHOLD);

	initializeConfigurator(NULL, NULL);

	time_t startTime = time(NULL);

	buildIndex();

	time_t middleTime = time(NULL);

	if (subIndexCount > 1) {
		// merge sub-indices into the final index
		IndexIterator **iterators = typed_malloc(IndexIterator*, subIndexCount);
		for (int i = 0; i < subIndexCount; i++) {
			char fileName[256];
			sprintf(fileName, "index.%04d", i);
			iterators[i] = CompactIndex::getIterator(
			fileName, (memoryLimit - CompactIndex::WRITE_CACHE_SIZE) / subIndexCount);
		}

		MultipleIndexIterator *iterator = new MultipleIndexIterator(iterators, subIndexCount);
		CompactIndex *target = CompactIndex::getIndex(NULL, "index.final", true);
		IndexMerger::mergeIndices(NULL, target, iterator, NULL, false);
		delete target;
		delete iterator;
		for (int i = 0; i < subIndexCount; i++) {
			char fileName[256];
			sprintf(fileName, "index.%04d", i);
			unlink(fileName);
		}
	}

	time_t endTime = time(NULL);

	int time1 = middleTime - startTime;
	int time2 = endTime - middleTime;
	printf("--------------------\n");
	printf("Memory limit: %d MB.\n", memoryLimit / 1024 / 1024);
	printf("Time to create %d sub-indices: %d seconds.\n", subIndexCount, time1);
	printf("Time to perform final merge operation: %d seconds.\n", time2);
	printf("Total time: %d seconds.\n", time1 + time2);
	printf("--------------------\n");

	return 0;
} // end of main(int, char**)


