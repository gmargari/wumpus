/**
 * This program reads a sequence of TREC-formatted documents from stdin and
 * prints performance statistics for different dictionary implementations
 * to stdout.
 *
 * author: Stefan Buettcher
 * created: 2006-12-07
 * changed: 2006-12-07
 **/


#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>
#include <ext/hash_map>
#include <ext/hash_fun.h>
#include "../filters/trec_inputstream.h"
#include "../misc/configurator.h"
#include "../misc/utils.h"


using namespace std;
using namespace __gnu_cxx;


struct TermEntry {
	char term[20];
	int32_t position;
};

struct HashtableEntry {
	TermEntry *term;
	HashtableEntry *next;
};


namespace __gnu_cxx {
	template<> struct hash<string> {
		size_t operator()(const string __s) const {
			return __stl_hash_string(__s.c_str());
		}
	};
}



static const int MAX_TOKEN_COUNT = 20000000;
static char *tokens[MAX_TOKEN_COUNT];
static int tokenCount = 0;


static void measureMapPerformance() {
	map<string,TermEntry*> dictionary;
	int start = currentTimeMillis();
	for (int i = 0; i < tokenCount; i++) {
		map<string,TermEntry*>::iterator iter = dictionary.find(tokens[i]);
		if (iter == dictionary.end()) {
			TermEntry *te = new TermEntry;
			strcpy(te->term, tokens[i]);
			te->position = -1;
			dictionary[tokens[i]] = te;
		}
	}
	int end = currentTimeMillis();
	printf("map<string,TermEntry>: %d milliseconds (%.1lf ns per token)\n",
	       end - start, (end - start) * 1E6 / tokenCount);
	printf("  Number of dictinct terms: %d\n\n", dictionary.size());
	for (map<string,TermEntry*>::iterator iter = dictionary.begin(); iter != dictionary.end(); ++iter)
		delete iter->second;
} // end of measureMapPerformance()


static void measureHashmapPerformance() {
	hash_map<string,TermEntry*> dictionary;
	int start = currentTimeMillis();
	for (int i = 0; i < tokenCount; i++) {
		hash_map<string,TermEntry*>::iterator iter = dictionary.find(string(tokens[i]));
		if (iter == dictionary.end()) {
			TermEntry *te = new TermEntry;
			strcpy(te->term, tokens[i]);
			te->position = -1;
			dictionary[tokens[i]] = te;
		}
	}
	int end = currentTimeMillis();
	printf("hash_map<string,TermEntry*>: %d milliseconds (%.1lf ns per token)\n",
	       end - start, (end - start) * 1E6 / tokenCount);
	printf("  Number of dictinct terms: %d\n\n", dictionary.size());
	for (hash_map<string,TermEntry*>::iterator iter = dictionary.begin(); iter != dictionary.end(); ++iter)
		delete iter->second;
} // end of measureHashmapPerformance()


static void measureHashtablePerformance(unsigned int tableSize, bool moveToFront, bool insertAtBack) {
	int termCount = 0;
	HashtableEntry **table = new HashtableEntry*[tableSize];
	memset(table, 0, tableSize * sizeof(table[0]));
	int start = currentTimeMillis();
	int comparisons = 0;
	for (int i = 0; i < tokenCount; i++) {
		unsigned int slot = simpleHashFunction(tokens[i]) % tableSize;
		HashtableEntry *prev = NULL, *runner = table[slot];
		while (runner != NULL) {
			comparisons++;
			if (strcmp(tokens[i], runner->term->term) == 0)
				break;
			prev = runner;
			runner = runner->next;
		}
		if (runner == NULL) {
			TermEntry *te = new TermEntry;
			strcpy(te->term, tokens[i]);
			te->position = -1;
			HashtableEntry *hte = new HashtableEntry;
			hte->term = te;
			if ((insertAtBack) && (prev != NULL)) {
				hte->next = NULL;
				prev->next = hte;
			}
			else {
				hte->next = table[slot];
				table[slot] = hte;
			}
			termCount++;
		}
		else if ((moveToFront) && (prev != NULL)) {
			prev->next = runner->next;
			runner->next = table[slot];
			table[slot] = runner;
		}
	}
	int end = currentTimeMillis();
	printf("hashtable<char*,TermEntry*>(%s/%s): %d milliseconds (%.1lf ns per token)\n",
	       moveToFront ? "move-to-front" : "no-move-to-front",
				 insertAtBack ? "insert-at-back" : "no-insert-at-back",
	       end - start, (end - start) * 1E6 / tokenCount);
	printf("  Number of dictinct terms: %d\n", termCount);
	printf("  Number of string comparisons: %d (%.1lf per token)\n\n",
	       comparisons, comparisons * 1.0 / tokenCount);
	for (int i = 0; i < tableSize; i++) {
		HashtableEntry *runner = table[i];
		while (runner != NULL) {
			HashtableEntry *next = runner->next;
			delete runner->term;
			delete runner;
			runner = next;
		}
	}
	delete[] table;
} // end of measureHashtablePerformance(int, bool, bool)


int main() {
	// initialize memory and make sure the address space is large enough
	initializeConfigurator();
	for (int i = 0; i < MAX_TOKEN_COUNT; i++)
		tokens[i] = new char[32];
	for (int i = 0; i < MAX_TOKEN_COUNT; i++)
		delete[] tokens[i];
	for (int i = 0; i < MAX_TOKEN_COUNT; i++)
		tokens[i] = new char[20];

	// read the first MAX_TOKEN_COUNT tokens from the input stream
	FilteredInputStream *inputStream = new TRECInputStream(fileno(stdin));
	int documentCount = 0;
	InputToken token;
	while ((inputStream->getNextToken(&token)) && (tokenCount < MAX_TOKEN_COUNT)) {
		if (strlen((char*)token.token) < 20) {
			strcpy(tokens[tokenCount++], (char*)token.token);
			if (strcmp((char*)token.token, "<doc>") == 0)
				if (++documentCount >= 10000)
					break;
		}
	}
	delete inputStream;
	assert(tokenCount > 0);

	printf("%d documents read from stdin (%d tokens). Measuring performance...\n\n",
	       documentCount, tokenCount);

	for (int i = 0; i < 3; i++) {
		measureMapPerformance();
		measureHashmapPerformance();
		for (int k = 1024; k <= 16384; k *= 16) {
			measureHashtablePerformance(k, false, false);
			measureHashtablePerformance(k, true, false);
			measureHashtablePerformance(k, false, true);
			measureHashtablePerformance(k, true, true);
		}
	}
	
	return 0;
} // end of main()



