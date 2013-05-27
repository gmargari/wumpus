/**
 * This program reads a sequence of TREC-formatted documents from stdin and
 * prints approximate index sizes for the document collection to stdout.
 *
 * author: Stefan Buettcher
 * created: 2006-11-26
 * changed: 2006-11-26
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../filters/trec_inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../misc/configurator.h"
#include "../misc/utils.h"


#define DOC_START_TAG "<doc>"
#define DOC_END_TAG "</doc>"

struct TermDescriptor {

	/** The term string's hashvalue. **/
	unsigned int hashValue;

	/** Last document in which we have seen this term. **/
	int prevDocument;

	/** Last within-document position at which we have seen this term. **/
	int prevPosition;

	/** TF value so far in current document. **/
	int currentTF;

	/** Last schema-independent position at which we have seen this term. **/
	long long prevSchemaIndependentPosition;

	/** Pointer to next element in hashtable. **/
	TermDescriptor *next;

}; // end of struct TermDescriptor


static const unsigned int HASHTABLE_SIZE = 1024 * 1024;

TermDescriptor *hashtable[HASHTABLE_SIZE];

static int termCount;
static int currentDocument = 0;
static int currentPosition = -1;
static long long currentSchemaIndependentPosition = -1;

static TermDescriptor *termBuffer = NULL;
static int allocated = 0;
static int used = 0;

double overhead = 0;
double docidSize = 0, docidSizeCompressed = 0;
double freqSize = 0, freqSizeCompressed = 0;
double posSize = 0, posSizeCompressed = 0;
double siSize = 0, siSizeCompressed = 0;


static int vByteSize(long long delta) {
	int result = 1;
	while (delta >= 128) {
		delta >>= 7;
		result++;
	}
	return result;
}


static void processToken(const char *token) {
	// look up term in hashtable
	unsigned int hashValue = simpleHashFunction(token);
	unsigned int slot = hashValue % HASHTABLE_SIZE;
	TermDescriptor *prev = NULL;
	TermDescriptor *runner = hashtable[slot];
	while (runner != NULL) {
		if (runner->hashValue == hashValue)
			break;
		prev = runner;
		runner = runner->next;
	}

	if (runner == NULL) {
		// if not found in hashtable, create new entry and put into table
		if (used >= allocated) {
			allocated = 1000;
			termBuffer = new TermDescriptor[allocated];
			used = 0;
		}
		runner = &termBuffer[used++];
		memset(runner, 0, sizeof(TermDescriptor));
		runner->hashValue = hashValue;
		runner->next = hashtable[slot];
		hashtable[slot] = runner;
		termCount++;
		overhead += strlen(token) + 32;
	}
	else {
		// otherwise, move entry to front of chain
		if (prev != NULL) {
			prev->next = runner->next;
			runner->next = hashtable[slot];
			hashtable[slot] = runner;
		}
	}

	if (currentDocument > runner->prevDocument) {
		docidSize += 4;
		docidSizeCompressed += vByteSize(currentDocument - runner->prevDocument - 1);
		runner->prevDocument = currentDocument;
		freqSize += 4;
		freqSizeCompressed += 1;
		runner->currentTF = 0;
		runner->prevPosition = -1;
	}
	freqSizeCompressed -= vByteSize(runner->currentTF);
	runner->currentTF++;
	freqSizeCompressed += vByteSize(runner->currentTF);

	posSize += 4;
	posSizeCompressed += vByteSize(currentPosition - runner->prevPosition);
	siSize += 4;
	siSizeCompressed +=
		vByteSize(currentSchemaIndependentPosition - runner->prevSchemaIndependentPosition);

	runner->prevPosition = currentPosition++;
	runner->prevSchemaIndependentPosition = currentSchemaIndependentPosition++;
} // end of processToken(char*)


int main() {
	memset(hashtable, 0, sizeof(hashtable));
	initializeConfigurator();
	FilteredInputStream *inputStream = new TRECInputStream(fileno(stdin));
	InputToken token;
	while (inputStream->getNextToken(&token)) {
LOOP_START:
		char *term = (char*)token.token;
		if (term[0] == '<') {
			if (strcasecmp(term, DOC_START_TAG) == 0) {
				currentDocument++;
				currentPosition = 0;
				processToken(term);
			}
			else if (strcasecmp(term, DOC_END_TAG) == 0) {
				if (currentDocument % 100000 == 0)
					fprintf(stderr, "%d documents done.\n", currentDocument);
				processToken(term);
				bool status = true;
				do {
					if (!(status = inputStream->getNextToken(&token)))
						break;
				} while (strcasecmp((char*)token.token, DOC_START_TAG) != 0);
				if (!status)
					break;
				goto LOOP_START;
			}
			else
				processToken(term);
		}
		else
			processToken(term);
	}
	delete inputStream;
	fprintf(stderr, "\n");

#if 0
	for (int i = 0; i < HASHTABLE_SIZE; i++) {
		for (TermDescriptor *runner = hashtable[i]; runner != NULL; runner = runner->next)
			printf("%s\n", runner->s);
		exit(0);
	}
#endif

	// compute cumulative numbers
	docidSize = docidSize + overhead;
	docidSizeCompressed = docidSizeCompressed + overhead;
	freqSize = freqSize + docidSize;
	freqSizeCompressed = freqSizeCompressed + docidSizeCompressed;
	posSize = posSize + freqSize;
	posSizeCompressed = posSizeCompressed + freqSizeCompressed;
	siSize += overhead;
	siSizeCompressed += overhead;
	
	printf("Total number of terms:          %10d\n", termCount);
	printf("Total number of tokens:         %10lld\n", currentSchemaIndependentPosition);
	printf("Total number of documents:      %10d\n", currentDocument);
	printf("\n");
	printf("Docid index:                    %10.1lf MB\n", docidSize/1024/1024);
	printf("Docid index (compressed):       %10.1lf MB\n", docidSizeCompressed/1024/1024);
	printf("Frequency index                 %10.1lf MB\n", freqSize/1024/1024);
	printf("Frequency index (compressed):   %10.1lf MB\n", freqSizeCompressed/1024/1024);
	printf("Positional index:               %10.1lf MB\n", posSize/1024/1024);
	printf("Positional index (compressed):  %10.1lf MB\n", posSizeCompressed/1024/1024);
	printf("SI index:                       %10.1lf MB\n", siSize/1024/1024);
	printf("SI index (compressed):          %10.1lf MB\n", siSizeCompressed/1024/1024);
	return 0;
} // end of main()



