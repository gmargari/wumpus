/**
 * A utility program that extracts tokens from a given document. The document
 * is tokenized by TRECInputStream (filters/trec_inputstream.h).
 * Run "tokenize_trec_document --help" to see a list of options.
 *
 * author: Stefan Buettcher
 * created: 2009-02-21
 * changed: 2009-02-21
 **/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "../filters/trec_inputstream.h"
#include "../misc/language.h"
#include "../stemming/stemmer.h"

using namespace std;

static const int TF_LINEAR = 1;
static const int TF_LOG = 2;
static const int TF_BINARY = 3;
static const int TF_BM25 = 4;

static void help() {
	printf("Usage:  tokenize_trec_document --file=FILENAME --offset=OFFSET \\\n");
	printf("          --stemming=[true|false] --tf=[linear|log|binary|bm25] \\\n");
	printf("          --dln=[true|false]\n\n");
	printf("- FILENAME is the name of the file that contains the document. Omit this \n");
	printf("  argument if you want to read from stdin.\n");
	printf("- OFFSET is the start offset (in bytes) of the document that you want to \n");
	printf("  tokenize. Omit this argument if you want to start at offset 0.\n");
	printf("- Porter stemming can be enabled or disabled via the --stemming option.\n");
	printf("  The default is false.\n");
	printf("- The --tf option can be used to produce linear, logarithmic, or binary\n");
	printf("  TF values. The default is linear.\n");
	printf("- Document length normalization for TF values is enabled through the --dln\n");
	printf("  option (only for linear or log TF values). The average document length\n");
	printf("  for normalization is assumed to be 1000.\n\n");
	exit(0);
}

static const char *getStringArgument(char** argv, int argc, const char *prefix) {
	const int prefixLen = strlen(prefix);
	for (int i = 0; i < argc; ++i)
		if (strncasecmp(argv[i], prefix, prefixLen) == 0)
			return argv[i] + prefixLen;
	return NULL;
}

static const char *getFilename(char** argv, int argc) {
	return getStringArgument(argv, argc, "--file=");
}

static const int getOffset(char** argv, int argc) {
	const char *offset = getStringArgument(argv, argc, "--offset=");
	if (offset == NULL) {
		return 0;
	} else {
		int value;
		assert(sscanf(offset, "%d", &value) == 1);
		return value;
	}
}

static bool getStemming(char** argv, int argc) {
	const char *stemming = getStringArgument(argv, argc, "--stemming=");
	if ((stemming == NULL) || (strcasecmp(stemming, "false") == 0))
		return false;
	if (strcasecmp(stemming, "true") == 0)
		return true;
	assert(false);
	return false;
}

static bool getDln(char** argv, int argc) {
	const char *dln = getStringArgument(argv, argc, "--dln=");
	if ((dln == NULL) || (strcasecmp(dln, "false") == 0))
		return false;
	if (strcasecmp(dln, "true") == 0)
		return true;
	assert(false);
	return false;
}

static int getTfMode(char** argv, int argc) {
	const char *tf = getStringArgument(argv, argc, "--tf=");
	if ((tf == NULL) || (strcasecmp(tf, "linear") == 0))
		return TF_LINEAR;
	if (strcasecmp(tf, "log") == 0)
		return TF_LOG;
	if (strcasecmp(tf, "binary") == 0)
		return TF_BINARY;
	if (strcasecmp(tf, "bm25") == 0)
		return TF_BM25;
	assert(false);
	return false;
}

static string readDocument(const char *filename, int offset) {
	// Open file.
	FILE *f = stdin;
	if (filename != NULL) {
		f = fopen(filename, "r");
		assert(f != NULL);
	}
	// Seek forward to the given offset.
	while (--offset >= 0)
		fgetc(f);
	string result;
	result.reserve(1000);
	// Read until EOF or "</doc>" is reached. Replace '\0' characters by whitespace.
	for (int x = fgetc(f); x != EOF; x = fgetc(f)) {
		const char c = (x == 0 ? ' ' : x);
		result.append(1, c);
		if (result.size() >= 6) {
			const string endOfDoc = result.substr(result.size() - 6);
			if ((endOfDoc == "</doc>") || (endOfDoc == "</DOC>"))
				break;
		}
		if (result.size() >= result.capacity() - 1)
			result.reserve(result.capacity() * 2);
		assert(result.size() < 10000000);  // if a document is longer than 10M, something is wrong
	}
	fclose(f);
	return result;
}

static void tokenizeDocument(const string& document, vector<string>* tokens) {
	FILE *f = tmpfile();
	fwrite(document.c_str(), 1, document.size(), f);
	rewind(f);
	TRECInputStream* tokenizer = new TRECInputStream(fileno(f));
	InputToken token;
	while (tokenizer->getNextToken(&token))
		tokens->push_back((char*)token.token);
	delete tokenizer;
	fclose(f);
}

static void getTfValues(const vector<string>& tokens, map<string, double>* tf) {
	tf->clear();
	for (int i = 0; i < tokens.size(); ++i)
		(*tf)[tokens[i]] += 1.0;
}

int main(int argc, char **argv) {
	for (int i = 0; i < argc; ++i)
		if (strcasecmp(argv[i], "--help") == 0)
			help();

  const string document =
		readDocument(getFilename(&argv[1], argc - 1), getOffset(&argv[1], argc - 1));

	vector<string> tokens;
	tokenizeDocument(document, &tokens);

	if (getStemming(&argv[1], argc - 1)) {
		// Apply Porter stemmer.
		for (int i = 0; i < tokens.size(); ++i) {
			char word[1024];
			strcpy(word, tokens[i].c_str());
			assert(word[0] != 0);
			Stemmer::stem(word, LANGUAGE_ENGLISH, true);
			if (word[0] != 0) {  // word[0] == 0 means the word is unstemmable (e.g., XML tag)
				strcat(word, "$");
				tokens[i] = word;
			}
		}
	}

	map<string, double> tfValues;
	getTfValues(tokens, &tfValues);

	if (getTfMode(&argv[1], argc - 1) != TF_BM25) {
		if (getDln(&argv[1], argc - 1)) {
			// Apply document length normalization.
			const double avgdl = 1000.0;
			const double correctionFactor = 0.5 + 0.5 * tokens.size() / avgdl;
			for (map<string, double>::iterator iter = tfValues.begin(); iter != tfValues.end(); ++iter)
				iter->second /= correctionFactor;
		}
	}

	switch (getTfMode(&argv[1], argc - 1)) {
		case TF_LINEAR:
			// Nothing to do.
			break;
		case TF_BINARY:
			// Replace all TF values with 1.0.
			for (map<string, double>::iterator iter = tfValues.begin(); iter != tfValues.end(); ++iter)
				iter->second = 1.0;
			break;
		case TF_LOG:
			for (map<string, double>::iterator iter = tfValues.begin(); iter != tfValues.end(); ++iter) {
				iter->second = 1.0 + log(max(iter->second, 1.0));  // max is necessary because of dln
				if (iter->second < 1E-3)
					iter->second = 1E-3;
			}
			break;
		case TF_BM25:
			for (map<string, double>::iterator iter = tfValues.begin(); iter != tfValues.end(); ++iter) {
				const double tf = iter->second;
				const double dl = tokens.size();
				const double avgdl = 568.0;  // avgdl for TREC45
				const double k1 = 1.2, b = 0.75;
				const double bm25score =
					tf * (k1 + 1.0) / (tf + k1 * (1.0 - b + b * dl / avgdl));
				iter->second = bm25score;
			}
			break;
	}

	// Print results.
	for (map<string, double>::iterator iter = tfValues.begin(); iter != tfValues.end(); ++iter)
		printf("%s\t%.3lf\n", iter->first.c_str(), iter->second);

  return 0;
}
