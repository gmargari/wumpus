/**
 * This program measures the index construction performance of the hash-based
 * method described in Chapter 5. It outputs performance figures for the initial
 * inversion step and for the final merge operation that brings together the
 * individual sub-indices.
 *
 * Temporary data (index files) will be written to the current working directory,
 * so make sure you are not sitting in an NFS mount.
 *
 * Usage:  measure_hashbased_indexing_performance STRATEGY MEMORY_LIMIT < INPUT_DATA
 *
 * STRATEGY is one of: REALLOC, LINKED_LIST, or GROUPING.
 * MEMORY_LIMIT is given in bytes and defines how much RAM the process may use.
 *
 * author: Stefan Buettcher
 * created: 2006-12-26
 * changed: 2007-09-08
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


static const int CONTAINER_SHIFT = 20;
static const int CONTAINER_SIZE = (1 << CONTAINER_SHIFT);


struct DictionaryEntry {
	DictionaryEntry *nextTerm;
	offset *postings;
	offset *nextPosting;
	int32_t spaceLeft;
	int32_t count;
	char term[20];
};


char *containers[2048];
int containerCount = 0;
int currentContainerPos = CONTAINER_SIZE;

static int termCount = 0;
static long totalNumberOfPostings = 0;

static const int HASHTABLE_SIZE = 65536;
static DictionaryEntry *hashtable[HASHTABLE_SIZE];

static int subIndexCount = 0;
static int memoryLimit = 0;
static int memoryConsumption = 0;


static inline int32_t allocateSpace(int32_t size) {
	if (currentContainerPos + size >= CONTAINER_SIZE)	{
		containers[containerCount++] = (char*)malloc(CONTAINER_SIZE);
		currentContainerPos = 0;
	}
	currentContainerPos += size;
	memoryConsumption += size;
	return ((containerCount - 1) << CONTAINER_SHIFT) + currentContainerPos - size;
} // end of allocateSpace(int)


static int compareLex(const void *a, const void *b) {
	DictionaryEntry *x = *(DictionaryEntry**)a;
	DictionaryEntry *y = *(DictionaryEntry**)b;
	return strcmp(x->term, y->term);
} // end of compareLex(const void*, const void*)


void writeIndexToDisk(bool isRealloc) {
	// sort terms in dictionary in lexicographical order
	DictionaryEntry **terms = typed_malloc(DictionaryEntry*, termCount);
	int outPos = 0;
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		for (DictionaryEntry *runner = hashtable[i]; runner != NULL; runner = runner->nextTerm)
			terms[outPos++] = runner;
	assert(outPos == termCount);
	qsort(terms, termCount, sizeof(DictionaryEntry*), compareLex);

	long long totalUsed = 0;
	long long totalUnused = 0;

#if 1
	// send all postings to the output index
	char fileName[256];
	sprintf(fileName, "index.%04d", subIndexCount++);
	CompactIndex *index = CompactIndex::getIndex(NULL, fileName, true);
	static const int BUFFER_SIZE = 256 * 1024;
	offset *buffer = typed_malloc(offset, BUFFER_SIZE);

	if (isRealloc) {
		for (int i = 0; i < termCount; i++) {
			index->addPostings(terms[i]->term, terms[i]->postings, terms[i]->count);
			totalUnused += terms[i]->spaceLeft + 1;
			totalUsed += terms[i]->count;
		}
	} // end if (isRealloc)
	else {
		for (int i = 0; i < termCount; i++) {
			int bufferPos = 0;
			offset *postings = terms[i]->postings; 
			offset *terminator = terms[i]->nextPosting;
			totalUnused += terms[i]->spaceLeft + 1;
			totalUsed += terms[i]->count;

			while (postings != terminator) {
				if (*postings < 0) {
					totalUnused++;
					int pos = -postings[0];
					postings = (offset*)&containers[pos >> CONTAINER_SHIFT][pos & (CONTAINER_SIZE - 1)];
				}
				else {
					buffer[bufferPos] = *(postings++);
					if (++bufferPos >= BUFFER_SIZE) {
						int toFlush = (int)(bufferPos * 0.75);
						index->addPostings(terms[i]->term, buffer, toFlush);
						memmove(buffer, &buffer[toFlush], (bufferPos - toFlush) * sizeof(offset));
						bufferPos -= toFlush;
					}
				}
			}
			index->addPostings(terms[i]->term, buffer, bufferPos);
		}
	} // end else [!isRealloc]

	free(buffer);
	delete index;
#endif

	// delete dictionary
	for (int i = 0; i < termCount; i++)
		free(terms[i]);
	free(terms);
	termCount = 0;
	memoryConsumption = 0;
	for (int i = 0; i < containerCount; i++)
		free(containers[i]);
	containerCount = 0;
	currentContainerPos = CONTAINER_SIZE;
	memset(hashtable, 0, sizeof(hashtable));

	printf("Space used:   %10lld bytes (%lld postings).\n",
			totalUsed * sizeof(offset), totalUsed);
	printf("Space unused: %10lld bytes (%lld postings).\n",
			totalUnused * sizeof(offset), totalUnused);
	printf("-----\n");
} // end of writeIndexToDisk(bool)


void indexLinkedList(int initialAllocation, double growthFactor) {
	offset indexAddress = 0;
	int myMemoryLimit = memoryLimit - CompactIndex::WRITE_CACHE_SIZE;

	InputToken token;
	TRECInputStream *tokenizer = new TRECInputStream(fileno(stdin));

	while (tokenizer->getNextToken(&token)) {
		// lookup term descriptor in dictionary
		char *term = (char*)token.token;
		unsigned int hashSlot = simpleHashFunction(term) % HASHTABLE_SIZE;
		DictionaryEntry *runner = hashtable[hashSlot], *prev = NULL;
		while (runner != NULL) {
			if (strcmp(runner->term, term) == 0)
				break;
			prev = runner;
			runner = runner->nextTerm;
		}

		if (runner == NULL) {
			// if the term is not in the dictionary, add a new entry
			runner = typed_malloc(DictionaryEntry, 1);
			memoryConsumption += 40;

			runner->nextTerm = NULL;
			strcpy(runner->term, term);
			int next = allocateSpace(8 * (initialAllocation + 1));
			runner->postings = runner->nextPosting =
				(offset*)&containers[next >> CONTAINER_SHIFT][next & (CONTAINER_SIZE - 1)];
			*(runner->nextPosting++) = ++indexAddress;
			runner->spaceLeft = initialAllocation - 1;
			runner->count = 1;
			if (prev == NULL)
				hashtable[hashSlot] = runner;
			else
				prev->nextTerm = runner;
			termCount++;
		}
		else {
			// add new posting to the term's in-memory postings list
			if (runner->spaceLeft <= 0) {
				int space = (int)(runner->count * growthFactor) + 1;
				if (space < initialAllocation)
					space = initialAllocation;
				if (space > 128)
					space = 128;
				int32_t next = allocateSpace(8 * (space + 1));
				*runner->nextPosting = -next;
				runner->nextPosting =
					(offset*)&containers[next >> CONTAINER_SHIFT][next & (CONTAINER_SIZE - 1)];
				runner->spaceLeft = space;
			}
			*(runner->nextPosting++) = ++indexAddress;
			runner->spaceLeft--;
			runner->count++;

			if (prev != NULL) {
				// move dictionary entry to front of collision list
				prev->nextTerm = runner->nextTerm;
				runner->nextTerm = hashtable[hashSlot];
				hashtable[hashSlot] = runner;
			}
		}

		if (memoryConsumption > myMemoryLimit)
			writeIndexToDisk(false);
	} // while (tokenizer->getNextToken(&token))
	if (termCount > 0)
		writeIndexToDisk(false);
	delete tokenizer;

	totalNumberOfPostings = indexAddress;
} // end of indexLinkedList(int, double)


void indexRealloc(int initialAllocation, double growthFactor) {
	offset indexAddress = 0;
	int myMemoryLimit = memoryLimit - CompactIndex::WRITE_CACHE_SIZE;

	InputToken token;
	TRECInputStream *tokenizer = new TRECInputStream(fileno(stdin));

	while (tokenizer->getNextToken(&token)) {
		// lookup term descriptor in dictionary
		char *term = (char*)token.token;
		unsigned int hashSlot = simpleHashFunction(term) % HASHTABLE_SIZE;
		DictionaryEntry *runner = hashtable[hashSlot], *prev = NULL;
		while (runner != NULL) {
			if (strcmp(runner->term, term) == 0)
				break;
			prev = runner;
			runner = runner->nextTerm;
		}

		if (runner == NULL) {
			// if the term is not in the dictionary, add a new entry
			runner = typed_malloc(DictionaryEntry, 1);
			memoryConsumption += 40;

			runner->nextTerm = NULL;
			strcpy(runner->term, term);
			int space = initialAllocation * sizeof(offset);
			runner->postings = (offset*)malloc(space);
			memoryConsumption += space;
			runner->postings[0] = ++indexAddress;
			runner->count = 1;
			runner->spaceLeft = initialAllocation - 1;
			if (prev == NULL)
				hashtable[hashSlot] = runner;
			else
				prev->nextTerm = runner;
			termCount++;
		}
		else  {
			if (runner->spaceLeft <= 0) {
				int space = (int)(runner->count * growthFactor) + 1;
				if (space < runner->count + initialAllocation)
					space = runner->count + initialAllocation;
				memoryConsumption += (space - runner->count) * sizeof(offset);
				runner->postings = (offset*)realloc(runner->postings, space * sizeof(offset));
				runner->spaceLeft = space - runner->count;
			}
			runner->postings[runner->count++] = ++indexAddress;
			runner->spaceLeft--;

			if (prev != NULL) {
				// move dictionary entry to front of collision list
				prev->nextTerm = runner->nextTerm;
				runner->nextTerm = hashtable[hashSlot];
				hashtable[hashSlot] = runner;
			}
		}

		if (memoryConsumption > myMemoryLimit) {
			// set end-of-list markers for all terms
			for (int i = 0; i < HASHTABLE_SIZE; i++)
				for (DictionaryEntry *runner = hashtable[i]; runner != NULL; runner = runner->nextTerm)
					runner->nextPosting = &runner->postings[runner->count];
			writeIndexToDisk(true);
		}
	} // while (tokenizer->getNextToken(&token))
	if (termCount > 0) {
		// set end-of-list markers for all terms
		for (int i = 0; i < HASHTABLE_SIZE; i++)
			for (DictionaryEntry *runner = hashtable[i]; runner != NULL; runner = runner->nextTerm)
				runner->nextPosting = &runner->postings[runner->count];
		writeIndexToDisk(true);
	}
	delete tokenizer;

	totalNumberOfPostings = indexAddress;
} // end of indexRealloc(int, double)


int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  measure_hashbased_indexing_performance STRATEGY MEMORY_LIMIT < INPUT_DATA\n\n");
		fprintf(stderr, "STRATEGY can be one of the following: LINKED_LIST, REALLOC, GROUPING.\n");
		fprintf(stderr, "MEMORY_LIMIT is the allowable memory consumption, in bytes.\n\n");
		return 1;
	}
	char *strategy = argv[1];
	int status = sscanf(argv[2], "%d", &memoryLimit);
	assert(status == 1);
	assert(memoryLimit > 2 * CompactIndex::WRITE_CACHE_SIZE);

	initializeConfigurator(NULL, NULL);

	// initialize hashtable
	memset(hashtable, 0, sizeof(hashtable));

	time_t startTime = time(NULL);

	// build sub-indices
	if (strcasecmp(strategy, "LINKED_LIST") == 0)
		indexLinkedList(1, 0);
	else if (strcasecmp(strategy, "REALLOC") == 0)
		indexRealloc(4, 1.30);
	else if (strcasecmp(strategy, "GROUPING") == 0)
		indexLinkedList(4, 0.30);
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
	printf("Memory limit: %d MB.\n", memoryLimit / 1024 / 1024);
	printf("Time to create %d sub-indices: %d seconds.\n", subIndexCount, time1);
	printf("Time to perform final merge operation: %d seconds.\n", time2);
	printf("Total time: %d seconds.\n", time1 + time2);
	printf("--------------------\n");

	return 0;
} // end of main(int, char**)



