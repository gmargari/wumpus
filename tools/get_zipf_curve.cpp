/**
 * This program reads a sequence of TREC-formatted documents from stdin and
 * prints the number of unique terms at certain landmarks to stdout.
 *
 * author: Stefan Buettcher
 * created: 2007-01-29
 * changed: 2007-01-29
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

	/** The term itself. **/
	char *term;

	/** Pointer to next element in hashtable. **/
	TermDescriptor *next;

}; // end of struct TermDescriptor


static const unsigned int HASHTABLE_SIZE = 1024 * 1024;

TermDescriptor *hashtable[HASHTABLE_SIZE];
static int termCount;

static TermDescriptor *termBuffer = NULL;
static int tbAllocated = 0;
static int tbUsed = 0;

static char *stringBuffer = NULL;
static int sbAllocated = 0;
static int sbUsed = 0;


static void processToken(const char *token) {
	// look up term in hashtable
	unsigned int hashValue = simpleHashFunction(token);
	unsigned int slot = hashValue % HASHTABLE_SIZE;
	TermDescriptor *prev = NULL;
	TermDescriptor *runner = hashtable[slot];
	while (runner != NULL) {
		if (runner->hashValue == hashValue)
			if (strcmp(runner->term, token) == 0)
				break;
		prev = runner;
		runner = runner->next;
	}

	if (runner == NULL) {
		// if not found in hashtable, create new entry and put into table
		if (tbUsed >= tbAllocated) {
			tbAllocated = 16384;
			termBuffer = new TermDescriptor[tbAllocated];
			tbUsed = 0;
		}
		if (sbUsed >= sbAllocated - 32) {
			sbAllocated = 16384;
			stringBuffer = new char[sbAllocated];
			sbUsed = 0;
		}
		runner = &termBuffer[tbUsed++];
		runner->hashValue = hashValue;
		runner->term = &stringBuffer[sbUsed];
		strcpy(runner->term, token);
		sbUsed += strlen(token) + 1;
		runner->next = hashtable[slot];
		hashtable[slot] = runner;
		termCount++;
	}
	else {
		// otherwise, move entry to front of chain
		if (prev != NULL) {
			prev->next = runner->next;
			runner->next = hashtable[slot];
			hashtable[slot] = runner;
		}
	}
} // end of processToken(char*)


int main() {
	long long target = 40000000000LL;
	target /= 4096;
	memset(hashtable, 0, sizeof(hashtable));
	initializeConfigurator();
	FilteredInputStream *inputStream = new TRECInputStream(fileno(stdin));
	InputToken token;
	long long tokenCount = 0;
	while (inputStream->getNextToken(&token)) {
		char *term = (char*)token.token;
		processToken(term);
#if 0
		if (++tokenCount >= target) {
			printf("%12lld tokens: %9d terms\n", tokenCount, termCount);
			target *= 2;
		}
#else
		if (++tokenCount % 1000000000 == 0) {
			printf("%12lld tokens: %9d terms\n", tokenCount, termCount);
		}
#endif
	}
	delete inputStream;

	return 0;
} // end of main()



