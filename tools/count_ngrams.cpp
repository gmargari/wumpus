/**
 * This program reads a sequence of TREC-formatted documents from stdin and
 * prints n-gram statistics to stdout.
 *
 * author: Stefan Buettcher
 * created: 2006-12-15
 * changed: 2006-12-18
 **/


#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <set>
#include <ext/hash_map>
#include <ext/hash_set>
#include "../filters/trec_inputstream.h"
#include "../misc/configurator.h"
#include "../misc/utils.h"


using namespace std;
using namespace __gnu_cxx;

namespace __gnu_cxx {
	template<> struct hash<string> {
		size_t operator()(const string __s) const {
			return __stl_hash_string(__s.c_str());
		}
	};
	template<> struct hash<long long> {
		size_t operator()(long long __l) const {
			return __l;
		}
	};
}


static const long long MAX_TERM_COUNT = 2000003;
typedef unsigned char byte;

int cnt = 0;
hash_map<string,int> terms;
set<long long> crap;

byte *unigrams = NULL;
byte *bigrams = NULL;
byte *trigrams = NULL;

unsigned long long previous[8];


static double getTrueCount(double count, double tableSize) {
	double trueCount = 0;
	double f = 0;
	while (f < count) {
		f = f + 1 - f / tableSize;
		trueCount++;
	}
	return trueCount;
}


int main(int argc, char **argv) {
	initializeConfigurator();

	static const int ARRAY_SIZE = 1500000001; //623456711;
	unigrams = (byte*)malloc(ARRAY_SIZE);
	memset(unigrams, 0, ARRAY_SIZE);
//	bigrams = (byte*)malloc(ARRAY_SIZE);
//	memset(bigrams, 0, ARRAY_SIZE);
//	trigrams = (byte*)malloc(ARRAY_SIZE);
//	memset(trigrams, 0, ARRAY_SIZE);
	
	unsigned long long tableSize = ARRAY_SIZE;
	tableSize *= 8;

	FilteredInputStream *inputStream = new TRECInputStream(fileno(stdin));
	InputToken token;
	long long tokenCount = 0;
	while (inputStream->getNextToken(&token)) {
		tokenCount++;
		if (tokenCount % 10000000 == 0)
			fprintf(stderr, "%lld tokens processed\n", tokenCount);
		char *term = (char*)token.token;
		assert(strlen(term) < 20);
		if (token.token[0] == '<')
			if (strcmp(term, "<doc>") == 0)
				cnt = 0;
		cnt++;

		unsigned long long termID = strlen(term);
		for (int i = 0; term[i] != 0; i++) {
			byte b = (byte)term[i];
			termID = termID * 127 + b;
		}
		previous[0] = previous[1];
		previous[1] = previous[2];
		previous[2] = termID;

		unsigned long long unigramIndex = previous[2];
		unigramIndex %= tableSize;

		unsigned long long bigramIndex =
			previous[1] * 50000017 + previous[2];
		bigramIndex %= tableSize;

		unsigned long long trigramIndex =
			((previous[0] * MAX_TERM_COUNT) + previous[1]) * MAX_TERM_COUNT + previous[2];
		trigramIndex %= tableSize;

		if ((cnt >= 1) && (unigrams != NULL))
			unigrams[unigramIndex >> 3] |= (1 << (unigramIndex & 7));
		if ((cnt >= 2) && (bigrams != NULL))
			bigrams[bigramIndex >> 3] |= (1 << (bigramIndex & 7));
		if ((cnt >= 3) && (trigrams != NULL))
			trigrams[trigramIndex >> 3] |= (1 << (trigramIndex & 7));
	}
	delete inputStream;

	printf("%lld tokens read from stdin.\n", tokenCount);
	if (unigrams != NULL) {
		long long unigramCount = 0;
		for (int i = 0; i < ARRAY_SIZE; i++)
			for (int k = 0; k < 8; k++)
				if (unigrams[i] & (1 << k))
					unigramCount++;
		printf("Number of unique unigrams: %.0lf\n", getTrueCount(unigramCount, tableSize));
	}
	if (bigrams != NULL) {
		long long bigramCount = 0;
		for (int i = 0; i < ARRAY_SIZE; i++)
			for (int k = 0; k < 8; k++)
				if (bigrams[i] & (1 << k))
					bigramCount++;
		printf("Number of unique bigrams: %.0lf\n", getTrueCount(bigramCount, tableSize));
	}
	if (trigrams != NULL) {
		long long trigramCount = 0;
		for (int i = 0; i < ARRAY_SIZE; i++)
			for (int k = 0; k < 8; k++)
				if (trigrams[i] & (1 << k))
					trigramCount++;
		printf("Number of unique trigrams: %.0lf\n", getTrueCount(trigramCount, tableSize));
	}
	
	return 0;
} // end of main()



