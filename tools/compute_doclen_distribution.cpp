#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include "../filters/xml_inputstream.h"
#include "../misc/configurator.h"
#include "../misc/utils.h"

using namespace std;

static const int FIRST_BUCKET_END = 100;

#if 1
static const char *DOCNO_START = "<DOCNO>";
static const char *DOCNO_END = "</DOCNO>";
#else
static const char *DOCNO_START = "<TITLE>";
static const char *DOCNO_END = "</TITLE>";
#endif

static int qrels_cnt[3] = {0, 0, 0};
static map<string,int> qrels;

static void readQrels(const char *fileName) {
	FILE *f = fopen(fileName, "r");
	assert(f != NULL);
	char line[65536];
	while (fgets(line, sizeof(line), f) != NULL) {
		char dummy[65536], docno[65536];
		int value;
		assert(sscanf(line, "%s%s%s%d%s", dummy, dummy, docno, &value, dummy) == 4);
		assert(strlen(docno) > 0);
		if (qrels.find(docno) == qrels.end()) {
			qrels[docno] = value;
			qrels_cnt[value]++;
		}
		else if (value > qrels[docno]) {
			qrels_cnt[qrels[docno]]--;
			qrels[docno] = value;
			qrels_cnt[value]++;
		}
	}
	fclose(f);
}

static int getTokenCount(char *documentData, int documentLength) {
	XMLInputStream tokenizer(documentData, documentLength, false);
	int lastSequenceNumber = 0;
	InputToken token;
	while (tokenizer.getNextToken(&token))
		lastSequenceNumber = token.sequenceNumber;
	return lastSequenceNumber + 1;
}

static int getBucket(int documentLength) {
  int result = 0;
	while (documentLength > FIRST_BUCKET_END) {
		documentLength /= 2;
		result++;
	}
	return result;
}

static void getDocno(char *documentData, string *docno) {
	char *docnoStart = strcasestr(documentData, DOCNO_START);
	if (docnoStart == NULL) {
		fprintf(stderr, "%s not found!\n", DOCNO_START);
		fprintf(stderr, "%s\n", documentData);
		assert(docnoStart != NULL);
	}
	docnoStart += strlen(DOCNO_START);
	while (*docnoStart == ' ') ++docnoStart;
	char *docnoEnd = strcasestr(docnoStart, DOCNO_END);
	if (docnoEnd == NULL) {
    char tmp[32];
		strncpy(tmp, docnoStart, 31);
		tmp[31] = 0;
		fprintf(stderr, "Warning: %s not found for docno: %s\n", DOCNO_END, tmp);
		*docno = tmp;
		return;
	}
	while (docnoEnd[-1] == ' ') --docnoEnd;
  *docnoEnd = 0;
  *docno = docnoStart;
	*docnoEnd = *DOCNO_END;

	for (int i = 0; i < docno->length(); i++)
		if ((*docno)[i] == ' ')
			(*docno)[i] = '_';
}

int main(int argc, char **argv) {
	initializeConfigurator(NULL, NULL);

	if (argc != 2) {
		fprintf(stderr, "Usage:  compute_doclen_distribution QRELS_FILE < CORPUS\n\n");
		return 1;
	}

	readQrels(argv[1]);

	long long allQrelsLength = 0, allDocsLength = 0, qrelsLength[3] = {0, 0, 0};
	int qrelsBuckets[3][20];
	int allQrelsBuckets[20];
	int allDocsBuckets[20];
	memset(allQrelsBuckets, 0, sizeof(allQrelsBuckets));
	memset(allDocsBuckets, 0, sizeof(allDocsBuckets));
	for (int i = 0; i <= 2; i++)
		memset(qrelsBuckets[i], 0, sizeof(qrelsBuckets[i]));

	int docCnt = 0;
  char *documentData = new char[8 * 1024 * 1024 + 32];
	int documentLength = 0, qrelsCoveredCount = 0;
	char line[65536];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if ((strncasecmp(line, "<DOC>", 5) == 0) && (documentLength > 0)) {
			if (++docCnt % 100000 == 0) {
				fprintf(stderr, "%d documents done. %d docs in qrels covered.\n", docCnt, qrelsCoveredCount);
			}
      int tokenCount = getTokenCount(documentData, documentLength);
			int bucket = getBucket(tokenCount);
			string docno;
			getDocno(documentData, &docno);
			allDocsLength += tokenCount;
			allDocsBuckets[bucket]++;
			map<string,int>::iterator iter = qrels.find(docno);
			if (iter != qrels.end()) {
				allQrelsLength += tokenCount;
				allQrelsBuckets[bucket]++;
				int value = iter->second;
				qrelsLength[value] += tokenCount;
				qrelsBuckets[value][bucket]++;
				qrelsCoveredCount++;
			}
			documentLength = 0;
		}
		int lineLen = strlen(line);
		assert(documentLength + lineLen < 8 * 1024 * 1024);
		memcpy(&documentData[documentLength], line, lineLen);
		documentLength += lineLen;
	}

	printf("Number of documents processed: %d.\n\n", docCnt);
	printf("Documents in qrels covered: %d/%d.\n", qrelsCoveredCount, qrels.size());
	printf("Avg. document length: %.2lf tokens.\n", allDocsLength * 1.0 / docCnt);
	printf("Avg. document length in qrels: %.2lf tokens.\n", allQrelsLength * 1.0 / qrels.size());
  for (int i = 0; i <= 2; i++)
		printf("Avg. document length in bucket %d: %.2lf tokens.\n",
		       i, qrelsLength[i] * 1.0 / qrels_cnt[i]);

	printf("allDocsBuckets:");
	for (int i = 0; i < 12; i++) printf(" %d", allDocsBuckets[i]);
	printf("\n");
	printf("allQrelsBuckets:");
	for (int i = 0; i < 12; i++) printf(" %d", allQrelsBuckets[i]);
	printf("\n");
	for (int v = 0; v <= 2; v++) {
		printf("qrelsBuckets[%d]:", v);
		for (int i = 0; i < 12; i++) printf(" %d", qrelsBuckets[v][i]);
		printf("\n");
	}
	printf("\n");

	return 0;
} // end of main(int, char**)



