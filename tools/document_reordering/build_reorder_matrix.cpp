#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX(a,b) (a > b ? a : b)

static const int MAX_URL_LENGTH = 48;

struct DocumentDescriptor {
	int docno;
	int tokenCount;
	int termCount;
	int url;
};

DocumentDescriptor *documents = NULL;
int allocatedForDocuments = 0;
int documentCount = 0;
char *urls = NULL;
int allocatedForURLs = 0;
int usedByURLs = 0;

static int compareByTokenCount(const void *a, const void *b) {
	DocumentDescriptor *x = (DocumentDescriptor*)a;
	DocumentDescriptor *y = (DocumentDescriptor*)b;
	return y->tokenCount - x->tokenCount;
}

static int compareByTermCount(const void *a, const void *b) {
	DocumentDescriptor *x = (DocumentDescriptor*)a;
	DocumentDescriptor *y = (DocumentDescriptor*)b;
	return y->termCount - x->termCount;
}

static int compareByUrl(const void *a, const void *b) {
	DocumentDescriptor *x = (DocumentDescriptor*)a;
	DocumentDescriptor *y = (DocumentDescriptor*)b;
	return strcmp(&urls[x->url], &urls[y->url]);
}

static void reverseString(char *s, int len) {
	for (int i = 0; i < len/2; i++) {
		char c = s[i];
		s[i] = s[len - 1 - i];
		s[len - 1 - i] = c;
	}
}

static void reverseHostName(char *s) {
	char *slash = strchr(s, '/');
	int len = 0;
	for (int i = 0; (s[i] != 0) && (s[i] != '/'); i++)
		len++;
	reverseString(s, len);
	int lastPeriod = -1;
	for (int i = 0; i <= len; i++) {
		if (s[i] == '.') {
			reverseString(&s[lastPeriod + 1], i - lastPeriod - 1);
			lastPeriod = i;
		}
	}
	reverseString(&s[lastPeriod + 1], len - lastPeriod - 1);
}

int main(int argc, char **argv) {
	if (argc != 2) {
USAGE:
		fprintf(stderr, "Usage:  build_reorder_matrix --CRITERION < DOCNO_URL_LIST > MATRIX\n\n");
		fprintf(stderr, "CRITERION may be one of: TOKEN_COUNT, TERM_COUNT, URL.\n\n");
		return 1;
	}
	if ((strcasecmp(argv[1], "--TOKEN_COUNT") != 0) &&
	    (strcasecmp(argv[1], "--TERM_COUNT") != 0) &&
	    (strcasecmp(argv[1], "--URL") != 0) &&
			(strcasecmp(argv[1], "--URL_REVERSE") != 0))
		goto USAGE;

	char line[1024];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if (line[0] == '#')
			continue;
		int docno, tokenCount, termCount;
		char docid[1024], url[1024];
		int status = sscanf(line, "%d%s%d%d%s", &docno, docid, &tokenCount, &termCount, url);
		assert(status == 5);
		assert(docno == documentCount);
		if (strncasecmp(url, "http://", 7) == 0)
			memmove(url, &url[7], sizeof(url) - 7);
		for (int i = 0; (url[i] != 0) && (url[i] != '/'); i++)
			if ((url[i] >= 'A') && (url[i] <= 'Z'))
				url[i] |= 32;
		if (strlen(url) > MAX_URL_LENGTH)
			url[MAX_URL_LENGTH] = 0;
		

		// copy URL into URL buffer
		if (usedByURLs + 256 > allocatedForURLs) {
			allocatedForURLs = (int)MAX(allocatedForURLs * 1.4, allocatedForURLs + 65536);
			urls = (char*)realloc(urls, allocatedForURLs);
		}
		int urlPos = usedByURLs;
		strcpy(&urls[usedByURLs], url);
		usedByURLs += strlen(url) + 1;

		// copy document descriptor
		if (documentCount >= allocatedForDocuments) {
			allocatedForDocuments = (int)MAX(allocatedForDocuments * 1.4, allocatedForDocuments + 8192);
			documents = (DocumentDescriptor*)realloc(documents, allocatedForDocuments * sizeof(DocumentDescriptor));
		}
		documents[docno].docno = docno;
		documents[docno].tokenCount = tokenCount;
		documents[docno].termCount = termCount;
		documents[docno].url = urlPos;
		documentCount++;
	}

	// sort documents by given criterion
	if (strcasecmp(argv[1], "--TOKEN_COUNT") == 0) {
		qsort(documents, documentCount, sizeof(DocumentDescriptor), compareByTokenCount);
	}
	else if (strcasecmp(argv[1], "--TERM_COUNT") == 0) {
		qsort(documents, documentCount, sizeof(DocumentDescriptor), compareByTermCount);
	}
	else if (strcasecmp(argv[1], "--URL") == 0) {
		qsort(documents, documentCount, sizeof(DocumentDescriptor), compareByUrl);
	}
	else if (strcasecmp(argv[1], "--URL_REVERSE") == 0) {
		for (int i = 0; i < documentCount; i++)
			reverseHostName(&urls[documents[i].url]);
		qsort(documents, documentCount, sizeof(DocumentDescriptor), compareByUrl);
	}

	// compute reordering matrix and print to stdout
	int *newOrdering = new int[documentCount];
	for (int i = 0; i < documentCount; i++)
		newOrdering[i] = -1;
	for (int i = 0; i < documentCount; i++)
		newOrdering[documents[i].docno] = i;
	printf("# OLD_DOCID NEW_DOCID\n");
	printf("DOCUMENT_COUNT: %d\n", documentCount);
	for (int i = 0; i < documentCount; i++) {
		assert(newOrdering[i] >= 0);
		printf("%d %d\n", i, newOrdering[i]);
	}

	return 0;
}


