/**
 * This program measures the index construction performance of four different
 * approaches to memory allocation for posting lists:
 *
 *   1. simple linked list, with 32-bit postings and 32-bit pointers
 *   2. two-pass indexing
 *   3. realloc (a la Heinz & Zobel)
 *   4. linked list with grouping
 *
 * Usage:  measure_allocation_performance STRATEGY OUTPUT_FILE INPUT_FILE_1 .. INPUT_FILE_N
 *
 * STRATEGY can be one of the following: LINKED_LIST, TWO_PASS, REALLOC, GROUPING.
 *
 * author: Stefan Buettcher
 * created: 2006-12-10
 * changed: 2007-07-13
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../filters/trec_inputstream.h"
#include "../index/compactindex.h"
#include "../misc/utils.h"


static const int CONTAINER_SHIFT = 20;
static const int CONTAINER_SIZE = (1 << CONTAINER_SHIFT);


struct DictionaryEntry {
	DictionaryEntry *nextTerm;
	int32_t *postings;
	int32_t *nextPosting;
	int32_t spaceLeft;
	int32_t count;
	char term[20];
};


char *containers[2048];
int containerCount = 0;
int currentContainerPos = CONTAINER_SIZE;

int termCount = 0;
int totalNumberOfPostings = 0;

static const int HASHTABLE_SIZE = 65536;
static DictionaryEntry *hashtable[HASHTABLE_SIZE];


static inline int32_t allocateSpace(int32_t size) {
	if (currentContainerPos + size >= CONTAINER_SIZE)	{
		containers[containerCount++] = new char[CONTAINER_SIZE];
		currentContainerPos = 0;
	}
	currentContainerPos += size;
	return ((containerCount - 1) << CONTAINER_SHIFT) + currentContainerPos - size;
} // end of allocateSpace(int)


void indexLinkedList(char **files, int fileCount, int initialAllocation, double growthFactor) {
	int32_t indexAddress = 0;
	for (int i = 0; i < fileCount; i++) {
		TRECInputStream *tokenizer = new TRECInputStream(files[i]);
		InputToken token;

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
				runner = new DictionaryEntry;
				runner->nextTerm = NULL;
				runner->count = 1;
				strcpy(runner->term, term);
				int next = allocateSpace(4 * (initialAllocation + 1));
				runner->postings = runner->nextPosting =
					(int32_t*)&containers[next >> CONTAINER_SHIFT][next & (CONTAINER_SIZE - 1)];
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
					if (space > 256)
						space = 256;
					int32_t next = allocateSpace(4 * (space + 1));
					*runner->nextPosting = -next;
					runner->nextPosting =
						(int32_t*)&containers[next >> CONTAINER_SHIFT][next & (CONTAINER_SIZE - 1)];
					runner->spaceLeft = space;
				}
				*(runner->nextPosting++) = ++indexAddress;
				runner->spaceLeft--;
				if (runner->count < 20000)
					runner->count++;

				if (prev != NULL) {
					// move dictionary entry to front of collision list
					prev->nextTerm = runner->nextTerm;
					runner->nextTerm = hashtable[hashSlot];
					hashtable[hashSlot] = runner;
				}
			}

		} // while (tokenizer->getNextToken(&token))

		delete tokenizer;
	}

	totalNumberOfPostings = indexAddress;
} // end of indexLinkedList(char**, int, int, bool)


void indexRealloc(char **files, int fileCount, int initialAllocation, double growthFactor) {
	int32_t indexAddress = 0;
	for (int i = 0; i < fileCount; i++) {
		TRECInputStream *tokenizer = new TRECInputStream(files[i]);
		InputToken token;

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
				runner = new DictionaryEntry;
				runner->nextTerm = NULL;
				strcpy(runner->term, term);
				runner->postings = (int32_t*)malloc(initialAllocation * sizeof(int32_t));
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
					runner->postings = (int32_t*)realloc(runner->postings, space * sizeof(int32_t));
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

		} // while (tokenizer->getNextToken(&token))

		delete tokenizer;
	}

	// set end-of-list markers for all terms
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		for (DictionaryEntry *runner = hashtable[i]; runner != NULL; runner = runner->nextTerm)
			runner->nextPosting = &runner->postings[runner->count];

	totalNumberOfPostings = indexAddress;
} // end of indexRealloc(char**, int, int, double)


void indexTwoPass(char **files, int fileCount) {
	// do a first pass to collect statistics
	for (int i = 0; i < fileCount; i++) {
		TRECInputStream *tokenizer = new TRECInputStream(files[i]);
		InputToken token;
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
				runner = new DictionaryEntry;
				runner->nextTerm = NULL;
				strcpy(runner->term, term);
				runner->count = 1;
				if (prev == NULL)
					hashtable[hashSlot] = runner;
				else
					prev->nextTerm = runner;
				termCount++;
			}
			else  {
				runner->count++;
				if (prev != NULL) {
					// move dictionary entry to front of collision list
					prev->nextTerm = runner->nextTerm;
					runner->nextTerm = hashtable[hashSlot];
					hashtable[hashSlot] = runner;
				}
			}
		} // while (tokenizer->getNextToken(&token))
		delete tokenizer;
	}

	// allocate memory for each term's postings list
	for (int i = 0; i < HASHTABLE_SIZE; i++) {
		for (DictionaryEntry *runner = hashtable[i]; runner != NULL; runner = runner->nextTerm) {
			runner->spaceLeft = runner->count;
			runner->postings = (int32_t*)malloc(runner->count * sizeof(int32_t));
			runner->count = 0;
		}
	}

	// do the second pass, building the actual index
	indexRealloc(files, fileCount, 0, 0);
} // end of indexTwoPass(char**, int)


static int compareLex(const void *a, const void *b) {
	DictionaryEntry *x = *(DictionaryEntry**)a;
	DictionaryEntry *y = *(DictionaryEntry**)b;
	return strcmp(x->term, y->term);
} // end of compareLex(const void*, const void*)


void writeIndexToDisk(char *fileName, char *strategy) {
	// sort terms in dictionary in lexicographical order
	DictionaryEntry **terms = new DictionaryEntry*[termCount];
	int outPos = 0;
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		for (DictionaryEntry *runner = hashtable[i]; runner != NULL; runner = runner->nextTerm)
			terms[outPos++] = runner;
	assert(outPos == termCount);
	qsort(terms, termCount, sizeof(DictionaryEntry*), compareLex);

	// send all postings to output index
	CompactIndex *index = CompactIndex::getIndex(NULL, fileName, true);
	static const int BUFFER_SIZE = 256 * 1024;
	offset *buffer = new offset[BUFFER_SIZE];

	for (int i = 0; i < termCount; i++) {
		int bufferPos = 0;
		int32_t *postings = terms[i]->postings; 
		int32_t *terminator = terms[i]->nextPosting;
		while (postings != terminator) {
			if (*postings < 0) {
				int pos = -postings[0];
				postings = (int32_t*)&containers[pos >> CONTAINER_SHIFT][pos & (CONTAINER_SIZE - 1)];
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

	delete[] buffer;
	delete index;

	long long dictionary = 0;
	long long postings = 0;
	if (strcasecmp(strategy, "LINKED_LIST") == 0) {
		postings = (containerCount - 1);
		postings = postings * CONTAINER_SIZE + currentContainerPos;
		dictionary = termCount * 12;
		for (int i = 0; i < termCount; i++)
			dictionary += strlen(terms[i]->term) + 1;
	}
	else if (strcasecmp(strategy, "GROUPING") == 0) {
		postings = (containerCount - 1);
		postings = postings * CONTAINER_SIZE + currentContainerPos;
		dictionary = termCount * 16;
		for (int i = 0; i < termCount; i++)
			dictionary += strlen(terms[i]->term) + 1;
	}
	else if (strcasecmp(strategy, "REALLOC") == 0) {
		dictionary = termCount * 12;
		for (int i = 0; i < termCount; i++) {
			dictionary += strlen(terms[i]->term) + 1;
			postings += 4 * (terms[i]->count + terms[i]->spaceLeft);
		}
	}
	else if (strcasecmp(strategy, "TWO_PASS") == 0) {
		dictionary = termCount * 12;
		for (int i = 0; i < termCount; i++) {
			dictionary += strlen(terms[i]->term) + 1;
			postings += 4 * terms[i]->count;
		}
	}

	printf("Dictionary size:    %10lld\n", dictionary);
	printf("Postings size:      %10lld\n", postings);
	printf("Total size:         %10lld\n", dictionary + postings);
	printf("Number of postings: %10d\n", totalNumberOfPostings);
} // end of writeIndexToDisk(char*, char*)


int main(int argc, char **argv) {
	initializeConfigurator(NULL, NULL);

	if (argc < 4) {
		fprintf(stderr, "Usage:  measure_allocation_performance STRATEGY OUTPUT_FILE INPUT_FILE_1 .. INPUT_FILE_N\n\n");
		fprintf(stderr, "STRATEGY can be one of the following: LINKED_LIST, TWO_PASS, REALLOC, GROUPING.\n\n");
		return 1;
	}
	char *strategy = argv[1];
	char *outputFile = argv[2];
	argv = &argv[3];
	argc -= 3;

	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = NULL;

	if (strcasecmp(strategy, "LINKED_LIST") == 0)
		indexLinkedList(argv, argc, 1, 0);
	else if (strcasecmp(strategy, "TWO_PASS") == 0)
		indexTwoPass(argv, argc);
	else if (strcasecmp(strategy, "REALLOC") == 0)
		indexRealloc(argv, argc, 3, 1.20);
	else if (strcasecmp(strategy, "GROUPING") == 0)
		indexLinkedList(argv, argc, 3, 0.20);
	else
		return main(0, NULL);

	writeIndexToDisk(outputFile, strategy);
	return 0;
} // end of main(int, char**)



