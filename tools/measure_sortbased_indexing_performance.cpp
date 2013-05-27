/**
 * This program measures the index construction performance of hash-based,
 * sort-based, and hybrid index construction. It outputs performance figures
 * for the initial inversion step and for the final merge operation that
 * brings together the individual sub-indices.
 *
 * Temporary data (index files) will be written to the current working directory,
 * so make sure you are not sitting in an NFS mount.
 *
 * Usage:  measure_hashbased_indexing_performance STRATEGY MEMORY_LIMIT < INPUT_DATA
 *
 * STRATEGY is one of: HASHING, SORTING, HYBRID.
 * MEMORY_LIMIT is given in bytes and defines how much RAM the process may use.
 *
 * author: Stefan Buettcher
 * created: 2007-09-08
 * changed: 2007-09-11
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


struct DictionaryEntry {
	uint32_t hashValue;    // this term's hash value (as computed by simpleHashValue)
	int32_t nextTerm;      // linking to next entry in same hash slot
	int32_t termID;        // numerical term ID
	int32_t postingCount;  // number of postings for this term
	char term[20];         // term string itself
};

static DictionaryEntry *dictionary = NULL;
static uint32_t termCount = 0, termsAllocated = 0;

uint64_t *postings = NULL;
static uint32_t postingCount = 0, maxPostingCount = 0;
static uint32_t totalNumberOfPostings = 0;

// every posting in the in-memory buffer consists of two parts,
// a term_id part and a position part; the combine the two as follows:
// posting = position + (term_id << TERMID_SHIFT)
static const int TERMID_SHIFT = 32;

static const int TERMID_BITWIDTH = 24;
static const int MAX_TERM_COUNT = (1 << TERMID_BITWIDTH) - 1;

static const int HASHTABLE_SIZE = 65536;
static int32_t hashTable[HASHTABLE_SIZE];

static uint32_t subIndexCount = 0;
static uint32_t memoryLimit = 0;
static uint32_t memoryConsumption = 0;


static int compareLex(const void *a, const void *b) {
	DictionaryEntry *x = (DictionaryEntry*)a;
	DictionaryEntry *y = (DictionaryEntry*)b;
	return strcmp(x->term, y->term);
} // end of compareLex(const void*, const void*)


/**
 * Sorts a bunch of postings using RadixSort. Postings are sorted in
 * ascending or descending order, depending on value of "ascending".
 **/
static void adjustAndSort(uint64_t *array, uint32_t n, uint32_t *idMap) {
#if 1
	static const int BITS_PER_PASS = 6;  // (1<<6) == 64 buckets result in best TLB performance
	static const int BUCKETS = (1 << BITS_PER_PASS);
	static const int MAX_BUCKET = (BUCKETS - 1);
	static const int PASSES = 10;
	assert(PASSES % 2 == 0);
	
	// collect statistics
	uint32_t cnt[PASSES][BUCKETS];
	memset(cnt, 0, sizeof(cnt));
	for (uint32_t k = 0; k < n; k++) {
		uint64_t value = array[k];
		uint64_t termID = idMap[value >> TERMID_SHIFT];
		array[k] = value = (value & 0xFFFFFFFF) | (termID << TERMID_SHIFT);
		for (int i = 0; i < PASSES; i++) {
			cnt[i][value & MAX_BUCKET]++;
			value >>= BITS_PER_PASS;
		}
	}

	// compute start positions of output chunks, from statistics gathered
	for (int i = 0; i < PASSES; i++) {
		uint32_t *c = cnt[i];
		c[MAX_BUCKET] = n - c[MAX_BUCKET];
		for (int k = MAX_BUCKET - 1; k >= 0; k--)
			c[k] = c[k + 1] - c[k];
		assert(c[0] == 0);
	}

	// perform radix-sort steps
	uint64_t *temp = typed_malloc(uint64_t, n);
	for (int i = 0; i < PASSES; i++) {
		uint32_t *c = cnt[i];
		unsigned int shift = i * BITS_PER_PASS;
		for (int k = 0; k < n; k++) {
			uint64_t value = array[k];
			uint32_t bucket = ((value >> shift) & MAX_BUCKET);
			temp[c[bucket]++] = value;
		}
		uint64_t *tmp = temp;
		temp = array;
		array = tmp;
	}
	free(temp);
#else
	static const int BITS_PER_PASS = 6;  // (1<<6) == 64 buckets result in best TLB performance
	static const int BUCKETS = (1 << BITS_PER_PASS);
	static const int MAX_BUCKET = (BUCKETS - 1);
	static const int PASSES = 4;
	assert(PASSES % 2 == 0);
	
	// collect statistics
	uint32_t cnt[PASSES][BUCKETS];
	memset(cnt, 0, sizeof(cnt));
	for (uint32_t k = 0; k < n; k++) {
		uint64_t value = array[k];
		uint64_t termID = idMap[value >> TERMID_SHIFT];
		array[k] = (value & 0xFFFFFFFF) | (termID << TERMID_SHIFT);
		for (int i = 0; i < PASSES; i++) {
			cnt[i][termID & MAX_BUCKET]++;
			termID >>= BITS_PER_PASS;
		}
	}

	// compute start positions of output chunks, from statistics gathered
	for (int i = 0; i < PASSES; i++) {
		uint32_t *c = cnt[i];
		c[MAX_BUCKET] = n - c[MAX_BUCKET];
		for (int k = MAX_BUCKET - 1; k >= 0; k--)
			c[k] = c[k + 1] - c[k];
		assert(c[0] == 0);
	}

	// perform radix-sort steps
	uint64_t *temp = typed_malloc(uint64_t, n);
	for (int i = 0; i < PASSES; i++) {
		uint32_t *c = cnt[i];
		unsigned int shift = TERMID_SHIFT + i * BITS_PER_PASS;
		for (int k = 0; k < n; k++) {
			uint64_t value = array[k];
			uint32_t bucket = ((value >> shift) & MAX_BUCKET);
			temp[c[bucket]++] = value;
		}
		uint64_t *tmp = temp;
		temp = array;
		array = tmp;
	}
	free(temp);
#endif
} // end of adjustAndSort(uint64_t*, uint32_t, uint32_t*)


void writeIndexToDisk() {
	printf("Writing %u postings to disk.\n", postingCount);

	// sort dictionary entries in lexicographical order
	qsort(dictionary, termCount, sizeof(DictionaryEntry), compareLex);
	uint32_t *idMap = typed_malloc(uint32_t, termCount);
	for (uint32_t i = 0; i < termCount; i++) {
		uint32_t termID = dictionary[i].termID;
		idMap[termID] = i;
	}

	// adjust postings to reflect new term IDs; sort postings by term ID
	adjustAndSort(postings, postingCount, idMap);
	free(idMap);

#if 1
	// send all postings to the output index
	char fileName[256];
	sprintf(fileName, "index.%04d", subIndexCount++);
	CompactIndex *index = CompactIndex::getIndex(NULL, fileName, true);
	uint32_t bufferPos = 0;
	for (int i = 0; i < termCount; i++) {
		uint32_t pCnt = dictionary[i].postingCount;
		for (uint32_t k = 0; k < pCnt; k++)
			postings[k + bufferPos] &= 0xFFFFFFFF;
		index->addPostings(dictionary[i].term, (offset*)&postings[bufferPos], pCnt);
		bufferPos += pCnt;
	}
	delete index;
#endif

	// delete dictionary and postings; reset hash table
	termCount = 0;
	postingCount = 0;
	memset(hashTable, 255, sizeof(hashTable));
} // end of writeIndexToDisk()


void indexRadixSort() {
	offset indexAddress = 0;
	uint32_t myMemoryLimit = memoryLimit - CompactIndex::WRITE_CACHE_SIZE;
	maxPostingCount = myMemoryLimit / sizeof(offset) / 2;
	postings = typed_malloc(uint64_t, maxPostingCount);
	postingCount = 0;

	InputToken token;
	TRECInputStream tokenizer(fileno(stdin));
	while (tokenizer.getNextToken(&token)) {
		// lookup term descriptor in dictionary
		char *term = (char*)token.token;
		uint32_t hashValue = simpleHashFunction(term);
		uint32_t hashSlot = hashValue % HASHTABLE_SIZE;
		int32_t runner = hashTable[hashSlot], prev = -1;
		while (runner >= 0) {
			DictionaryEntry *de = &dictionary[runner];
			if (de->hashValue == hashValue)
				if (strcmp(de->term, term) == 0)
					break;
			prev = runner;
			runner = de->nextTerm;
		}

		uint64_t termID = runner;
		if (runner < 0) {
			// if the term is not in the dictionary, add a new entry
			if (termCount >= termsAllocated) {
				if (termsAllocated == 0) {
					termsAllocated = 65536;
					dictionary = typed_malloc(DictionaryEntry, termsAllocated);
				}
				else {
					termsAllocated *= 2;
					dictionary = typed_realloc(DictionaryEntry, dictionary, termsAllocated);
				}
			}
			strcpy(dictionary[termCount].term, term);
			dictionary[termCount].hashValue = hashValue;
			dictionary[termCount].nextTerm = -1;
			dictionary[termCount].termID = termCount;
			dictionary[termCount].postingCount = 1;
			if (prev < 0)
				hashTable[hashSlot] = termCount;
			else
				dictionary[prev].nextTerm = termCount;
			termID = termCount++;
			postings[postingCount++] = (++indexAddress) + (termID << TERMID_SHIFT);
			if ((postingCount >= maxPostingCount) || (termCount >= MAX_TERM_COUNT))
				writeIndexToDisk();
		}
		else {
			if (prev >= 0) {
				// move dictionary entry to front of collision list
				dictionary[prev].nextTerm = dictionary[runner].nextTerm;
				dictionary[runner].nextTerm = hashTable[hashSlot];
				hashTable[hashSlot] = runner;
			}
			dictionary[termID].postingCount++;
			postings[postingCount++] = (++indexAddress) + (termID << TERMID_SHIFT);
			if (postingCount >= maxPostingCount)
				writeIndexToDisk();
		}
	} // while (tokenizer->getNextToken(&token))
	if (postingCount > 0)
		writeIndexToDisk();

	free(dictionary);
	free(postings);
	totalNumberOfPostings = indexAddress;
} // end of indexRadixSort()


int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  measure_sortbased_indexing_performance STRATEGY MEMORY_LIMIT < INPUT_DATA\n\n");
		fprintf(stderr, "STRATEGY can be one of the following: RADIX_SORT.\n");
		fprintf(stderr, "MEMORY_LIMIT is the allowable memory consumption, in bytes.\n\n");
		return 1;
	}
	char *strategy = argv[1];
	int status = sscanf(argv[2], "%d", &memoryLimit);
	assert(status == 1);
	assert(memoryLimit > 2 * CompactIndex::WRITE_CACHE_SIZE);
	
	initializeConfigurator(NULL, NULL);

	// initialize hashTable
	memset(hashTable, 255, sizeof(hashTable));

	time_t startTime = time(NULL);

	// build sub-indices
	if (strcasecmp(strategy, "RADIX_SORT") == 0)
		indexRadixSort();
	else
		return main(0, NULL);

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



