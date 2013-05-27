/**
 * Reads a Charlie-formatted TREC collection from stdin and prints lines of
 * the form
 *
 *   DOCNO DOCID #TOKENS #TERMS URL
 *
 * to stdout, one line per document.
 *
 * author: Stefan Buettcher
 * created: 2006-12-25
 * changed: 2006-12-25
 **/


#include <assert.h>
#include <stdio.h>
#include <set>
#include <string>
#include "../../filters/trec_inputstream.h"


using namespace std;


static const int MAX_DOCUMENT_SIZE = 4 * 1024 * 1024;
char document[MAX_DOCUMENT_SIZE];
int documentSize = 0;

int documentCount = 0;
char docid[65536];
char url[65536];

void processDocument() {
	static char *fileName = "/tmp/docdata.txt";
	FILE *f = fopen(fileName, "w");
	assert(f != NULL);
	fwrite(document, 1, documentSize, f);
	fclose(f);
	documentSize = 0;

	TRECInputStream *input = new TRECInputStream(fileName);
	set<string> terms;
	int tokenCount = 0;
	InputToken token;
	while (input->getNextToken(&token)) {
		terms.insert((char*)token.token);
		tokenCount++;
	}
	delete input;
	unlink(fileName);

	int len = strlen(url);
	assert(len > 0);
	assert(strchr(url, ' ') == NULL);
	assert(strchr(url, '\t') == NULL);
	if (len > 200)
		strcpy(&url[197], "...");
	if (url[len - 1] == '\n')
		url[len - 1] = 0;
	printf("%d %s %d %d %s\n", documentCount, docid, tokenCount, terms.size(), url);
	documentCount++;
}


int main() {
	printf("# DOCNO DOCID TOKENS TERMS URL\n");
	int len;
	char line[65536];
	bool docnoSeenForCurrentDocument = false;
	while (fgets(line, sizeof(line), stdin) != NULL) {
START:
		len = strlen(line);
		if (documentSize + len < MAX_DOCUMENT_SIZE) {
			memcpy(&document[documentSize], line, len);
			document[documentSize = documentSize + len] = 0;
		}
		if (line[0] == '<') {
			if (strncmp(line, "</DOC>", 6) == 0) {
				processDocument();
				docnoSeenForCurrentDocument = false;
			}
			else if ((strncmp(line, "<DOCNO>", 7) == 0) && (!docnoSeenForCurrentDocument)) {
				char *p = &line[7];
				char *q = strchr(p, '<');
				assert(q != NULL);
				*q = 0;
				strcpy(docid, p);
				docnoSeenForCurrentDocument = true;
			}
			else if (strncmp(line, "<DOCHDR>", 7) == 0) {
				fgets(line, sizeof(line), stdin);
				strcpy(url, line);
				goto START;
			}
		}
	}
	return 0;
}


