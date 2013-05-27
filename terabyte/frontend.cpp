/**
 * Copyright (C) 2007 Stefan Buettcher. All rights reserved.
 * This is free software with ABSOLUTELY NO WARRANTY.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA
 **/

/**
 * This is the frontend for the TREC Terabyte retrieval. The frontend reads
 * TREC-formatted topics from stdin, sends the resulting queries to the engine(s),
 * collects the results, and writes the TREC-formatted output to stdout.
 *
 * Command-line parameters:
 *
 *   ./frontend SERVER_1 PORT_1 .. SERVER_N PORT_N N=XXXXX MODE=[TREC|PLAIN|WUMPUS]
 *
 * N is the number of documents to be returned by the frontend.
 *
 * author: Stefan Buettcher
 * created: 2005-06-10
 * changed: 2006-01-25
 **/


#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <resolv.h>
#include <string.h>
#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/types.h>
#include <unistd.h>
#include "terabyte.h"
#include "../misc/all.h"
#include "../misc/stopwords.h"


#define RUN_ID "Wumpus"

static const bool LOGGING = true;

static const int MAX_SERVER_COUNT = 32;

static const char *FEEDBACK_MODE = ""; //[feedback=okapi]";

static const char *QUERY_COMMAND = "@bm25[docid][ctr=0.2]";

static const bool REMOVE_STOPWORDS = false;

static const bool STEMMING = false;

static const char *WWF_STRING = " with weights from (\"<doc>\"..\"</doc>\")>(%s)";

static const bool INCLUDE_WWF = false;

static int serverCount;
static char *hostName[MAX_SERVER_COUNT];
static int portNumber[MAX_SERVER_COUNT];
static int serverConnection[MAX_SERVER_COUNT];
static FILE *serverConn[MAX_SERVER_COUNT];
static int documentCount;
static char topicID[256];

static const int MAX_TOKEN_LEN = 18;
static const int MAX_DOCID_LEN = 31;
static const int MAX_RESULT_COUNT = 20000;

static const bool REPLACE_US_AND_UK = false;

char translationTable[256];

struct WeightedTerm {
	char term[MAX_TOKEN_LEN * 2];
	double weight;
};

struct ScoredDocument {
	char docID[MAX_DOCID_LEN + 1];
	double score;
};

static char *topicFields[4] = {
	"<title>",
	"<desc> Description:",
	"<narr> Narrative:",
	NULL
};

// T: (1.0, 0.0, 0.0), TDN: (0.7, 0.2, 0.1)
static double topicFieldWeights[4] = {
	1.0,
	0.0,
	0.0,
	0.0
};

static ScoredDocument results[MAX_RESULT_COUNT];


/** Prints general usage and terminates the program. **/
void printUsage() {
	fprintf(stderr, "Usage: ./frontend SERVER_1 PORT_1 .. SERVER_N PORT_N N=XXXXX MODE=[TREC|PLAIN|WUMPUS]\n\n");
	fprintf(stderr, "N is the number of documents to be returned. MODE is used to switch between ");
	fprintf(stderr, "TREC-formatted input topics, plain text (one topic per line), and Wumpus queries ");
	fprintf(stderr, "(@okapi ...).\n\n");
	exit(1);
} // end of printUsage()


/** Opens TCP connections to all servers specified in the command-line. **/
void connectToServers() {
	char dummy[1024];
	for (int i = 0; i < serverCount; i++) {
		serverConnection[i] = socket(PF_INET, SOCK_STREAM, 0);
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		inet_aton(hostName[i], &addr.sin_addr);
		addr.sin_port = htons(portNumber[i]);
		if (connect(serverConnection[i], (sockaddr*)&addr, sizeof(addr)) != 0) {
			fprintf(stderr, "Unable to connect to server: %s\n", hostName[i]);
			exit(1);
		}
		serverConn[i] = fdopen(serverConnection[i], "a+");
		assert(serverConn[i] != NULL);
		fgets(dummy, sizeof(dummy), serverConn[i]);
	}
} // end of connectToServers()


void closeServerConnections() {
	for (int i = 0; i < serverCount; i++)
		fclose(serverConn[i]);
} // end of closeServerConnections()


static int compareByTerm(const void *a, const void *b) {
	WeightedTerm *x = (WeightedTerm*)a;
	WeightedTerm *y = (WeightedTerm*)b;
	return strcmp(x->term, y->term);
}


static int compareByWeight(const void *a, const void *b) {
	WeightedTerm *x = (WeightedTerm*)a;
	WeightedTerm *y = (WeightedTerm*)b;
	if (x->weight > y->weight)
		return -1;
	else if (x->weight < y->weight)
		return +1;
	else
		return 0;
}


static int compareByScore(const void *a, const void *b) {
	ScoredDocument *x = (ScoredDocument*)a;
	ScoredDocument *y = (ScoredDocument*)b;
	if (x->score > y->score)
		return -1;
	else if (x->score < y->score)
		return +1;
	else
		return 0;
}


void replaceUSandUK(char *line) {
	char *toReplace;
	char *temp = (char*)malloc(strlen(line) + 8);
	while ((toReplace = strstr(line, " U.S. ")) != NULL) {
		*toReplace = 0;
		sprintf(temp, "%s%s%s", line, " USA ", &toReplace[6]);
		strcpy(line, temp);
	}
	while ((toReplace = strstr(line, " u.s. ")) != NULL) {
		*toReplace = 0;
		sprintf(temp, "%s%s%s", line, " USA ", &toReplace[6]);
		strcpy(line, temp);
	}
	while ((toReplace = strstr(line, " u s ")) != NULL) {
		*toReplace = 0;
		sprintf(temp, "%s%s%s", line, " USA ", &toReplace[5]);
		strcpy(line, temp);
	}
	while ((toReplace = strstr(line, " US ")) != NULL) {
		*toReplace = 0;
		sprintf(temp, "%s%s%s", line, " USA ", &toReplace[4]);
		strcpy(line, temp);
	}
	while ((toReplace = strstr(line, " U.K. ")) != NULL) {
		*toReplace = 0;
		sprintf(temp, "%s%s%s", line, " UK ", &toReplace[6]);
		strcpy(line, temp);
	}
	free(temp);
} // end of replaceUSandUK(char*)


void adjustScoresAccordingToDocumentID(ScoredDocument *results, int resultCount) {
	for (int i = 0; i < resultCount; i++) {
		if (strncmp(results[i].docID, "GX", 2) != 0) {
			results[i].score -= 100;
			continue;
		}
		int a = results[i].docID[2];
		int b = results[i].docID[3];
		int c = results[i].docID[4];
		int d = results[i].docID[6];
		int e = results[i].docID[7];
		int crawlRank = e + d * 10 + c * 100 + b * 1000 + a * 10000;
		results[i].score -= log(crawlRank) / log(5);
	}
} // end of adjustScoresAccordingToDocumentID(ScoredDocument*, int)


void processQuery(char *queryString) {
	char line[1024], dummy[1024], queryID[1024], docID[1024];
	if (LOGGING)
		fprintf(stderr, "%s\n", queryString);
	for (int i = 0; i < serverCount; i++)
		fprintf(serverConn[i], "%s\n", queryString);
	for (int i = 0; i < serverCount; i++)
		fflush(serverConn[i]);

	bool unsortedResults = (serverCount > 1);
	int resultCount = 0;

	for (int i = 0; i < serverCount; i++) {
		int resultsFromThisServer = 0;

		while (true) {
			line[0] = 0;
			if (fgets(line, sizeof(line), serverConn[i]) == NULL)
				break;
			if ((line[0] == '@') && (LOGGING)) {
				fprintf(stderr, "[%s] %s", hostName[i], line);
				if (line[strlen(line) - 1] != '\n')
					fprintf(stderr, "\n");
				break;
			}

			if (resultsFromThisServer >= MAX_RESULT_COUNT / serverCount)
				continue;

			if ((strncasecmp(queryString, "@qap", 4) == 0) || (strncasecmp(queryString, "@tier", 4) == 0)) {
				sscanf(line, "%s%lf%s%s%s%s%s", queryID, &results[resultCount].score, dummy, dummy, dummy, dummy, docID);
				if (strstr(docID, "<DOCNO>") != NULL) {
					strcpy(dummy, &strstr(docID, "<DOCNO>")[strlen("<DOCNO>") - 1]);
					strcpy(docID, dummy);
				}
				if (strstr(docID, "</DOCNO>") != NULL)
					strstr(docID, "</DOCNO>")[0] = 0;
			}
			else
				sscanf(line, "%s%lf%s%s%s", queryID, &results[resultCount].score, dummy, dummy, docID);

			int len = strlen(docID);
			if (len <= MAX_DOCID_LEN) {
				docID[--len] = 0;
				strcpy(results[resultCount].docID, &docID[1]);
				resultCount++;
				resultsFromThisServer++;
			}
		}
	}

#if 0
	unsortedResults = true;
	adjustScoresAccordingToDocumentID(results, resultCount);
#endif

	if (unsortedResults)
		qsort(results, MIN(resultCount, documentCount),
				sizeof(ScoredDocument), compareByScore);

	for (int i = 0; (i < resultCount) && (i < documentCount); i++) {
		if (strcmp(results[i].docID, "n/a") == 0)
			sprintf(results[i].docID, "GX%03d-%02d-%07d",
					random() % 272, random() % 100, random() % 10000000);
		printf("%s Q0 %s %d %.4lf %s\n",
				queryID, results[i].docID, i + 1, results[i].score, RUN_ID);
	}
	if (resultCount == 0)
		printf("%s Q0 %s %d %.4lf %s\n", topicID, "GX000-00-0000000",
				   1, 1.0, RUN_ID);
} // end of processQuery(char*)


void processQuery(WeightedTerm *query, int queryLen, char *queryString) {
	char wwf[8192], phrase[8192];
	int wwfLen = 0;
	int queryStringLen = strlen(queryString);
	phrase[0] = phrase[1] = 0;

	if (strncmp(QUERY_COMMAND, "@bm25tera", 9) != 0)
		queryStringLen += sprintf(&queryString[queryStringLen], " \"<doc>\"..\"</doc>\" by ");

	for (int i = 0; i < queryLen; i++) {
		for (int k = i + 1; k < queryLen; k++)
			if (strcmp(query[i].term, query[k].term) == 0) {
				query[i].weight += query[k].weight;
				query[k].weight = 0.0;
			}
		if (query[i].weight > 0.001) {
			queryStringLen += sprintf(&queryString[queryStringLen],
					"%s #%.3lf \"%s%s\"", (wwfLen == 0 ? "" : ","), query[i].weight,
					(STEMMING ? "$" : ""), query[i].term);
#if 0
			if (i < queryLen - 1) if (query[i + 1].weight > 0.001) {
				queryStringLen += sprintf(&queryString[queryStringLen],
						", #0.5 \"%s %s\"", query[i].term, query[i + 1].term);
			}
#endif
			sprintf(&phrase[strlen(phrase)], " %s", query[i].term);
			wwfLen += sprintf(&wwf[wwfLen], "%s\"%s%s\"", (wwfLen == 0 ? "" : "+"),
					(STEMMING ? "$" : ""), query[i].term);
		}
	}
#if 0
	queryStringLen += sprintf(&queryString[queryStringLen], ", \"%s\"", &phrase[1]);
#endif
	if (INCLUDE_WWF)
		queryStringLen += sprintf(&queryString[queryStringLen], WWF_STRING, wwf);
	if (queryLen > 0)
		processQuery(queryString);
} // end of processQuery(WeightedTerm*, int, char*)


void processTREC() {
	char topic[32768];
	char line[32768];
	char thisField[32768];
	int lineLen, topicLen, queryLen;
	WeightedTerm query[1024];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if ((line[0] == 0) || (line[0] == '\n'))
			continue;
		line[strlen(line) - 1] = ' ';
		if (strncasecmp(line, "<top>", 5) == 0) {
			topicLen = 0;
		}
		else if (strncasecmp(line, "</top>", 6) == 0) {
			topicLen += sprintf(&topic[topicLen], "%s", line);
			int outPos = 1;
			for (int i = 1; topic[i] != 0; i++)
				if ((topic[i] != ' ') || (topic[outPos - 1] != ' '))
					topic[outPos++] = topic[i];
			topic[outPos] = 0;
			queryLen = 0;
			char *topicColon = strstr(topic, "Topic:");
			if (topicColon != NULL)
				for (int i = 0; i < 6; i++)
					topicColon[i] = ' ';
			for (int i = 0; topicFields[i] != NULL; i++) if (topicFieldWeights[i] > 0.0) {
				char *field = strstr(topic, topicFields[i]);
				if (field == NULL) {
					fprintf(stderr, "Error: Field \"%s\" not found.\n", topicFields[i]);
					continue;
				}
				field = &field[strlen(topicFields[i])];
				char *endOfField = strchr(field, '<');
				if (endOfField == NULL) {
					fprintf(stderr, "Error: No delimiter found for field \"%s\".\n", topicFields[i]);
					continue;
				}
				*endOfField = 0;
				strcpy(thisField, field);
				*endOfField = '<';
				if (REPLACE_US_AND_UK)
					replaceUSandUK(thisField);
				for (int k = 0; thisField[k] != 0; k++) {
					if (thisField[k] < 0)
						thisField[k] = ' ';
					else
						thisField[k] = translationTable[thisField[k]];
				}

				char *token = strtok(thisField, " ");
				while (token != NULL) {
					if (strlen(token) < MAX_TOKEN_LEN) {
						if ((!REMOVE_STOPWORDS) || (!isStopword(token, LANGUAGE_ENGLISH))) {
							strcpy(query[queryLen].term, token);
							query[queryLen].weight = topicFieldWeights[i];
							queryLen++;
						}
					}
					token = strtok(NULL, " ");
				} // end  while (token != NULL)
			} // end for (int i = 0; topicFields[i] != NULL; i++)

			char *num = strstr(topic, "Number:");
			sscanf(&num[strlen("Number:")], "%s", topicID);
			char queryString[32768];
			int queryStringLen = 0;
			queryStringLen += sprintf(&queryString[queryStringLen], QUERY_COMMAND);
			queryStringLen += sprintf(&queryString[queryStringLen],
					"[count=%d][id=%s]%s", documentCount, topicID, FEEDBACK_MODE);
			processQuery(query, queryLen, queryString);
		}
		else
			topicLen += sprintf(&topic[topicLen], "%s", line);
	}
} // end of processTREC()


void processPlain() {
	char line[32768];
	char wumpusQuery[32768];
	int lineLen, topicLen, queryLen;
	WeightedTerm query[1024];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if ((line[0] == 0) || (line[0] == '\n'))
			continue;
		line[strlen(line) - 1] = 0;

		if (line[0] == '@') {
			processQuery(line);
			continue;
		}

		if (REPLACE_US_AND_UK)
			replaceUSandUK(line);
		for (int k = 0; line[k] != 0; k++) {
			if (line[k] < 0)
				line[k] = ' ';
			else
				line[k] = translationTable[line[k]];
		}

		// normalize input string
		for (int i = 0; line[i] != 0; i++) {
			if ((line[i] >= 'a') && (line[i] <= 'z')) {
				line[i] = line[i];
			}
			else if ((line[i] >= 'A') && (line[i] <= 'Z')) {
				line[i] = (line[i] | 32);
			}
			else if ((line[i] >= '0') && (line[i] <= '9')) {
				line[i] = line[i];
			}
			else
				line[i] = ' ';
		} // end for (int i = 0; line[i] != 0; i++)

		char *token = strtok(line, " ");
		int queryLen = 0;
		while (token != NULL) {
			if (strlen(token) < MAX_TOKEN_LEN) {
				if (!(REMOVE_STOPWORDS) || (!isStopword(token, LANGUAGE_ENGLISH))) {
					if ((queryLen == 0) && (strcmp(token, "us") == 0))
						strcpy(query[queryLen].term, "usa");
					else
						strcpy(query[queryLen].term, token);
					query[queryLen].weight = 1.0;
					queryLen++;
				}
			}
			else {
				token[MAX_TOKEN_LEN - 3] = 0;
				strcpy(query[queryLen].term, token);
				query[queryLen].weight = 1.0;
				queryLen++;
			}
			token = strtok(NULL, " ");
		} // end  while (token != NULL)

		char queryString[32768];
		int queryID, queryStringLen = 0;
		queryStringLen += sprintf(&queryString[queryStringLen], "%s", QUERY_COMMAND);
		queryStringLen += sprintf(&queryString[queryStringLen], "[count=%d]", documentCount);
		if (sscanf(query[0].term, "%d", &queryID) == 1) {
			sprintf(topicID, "%d", queryID);
			queryStringLen += sprintf(&queryString[queryStringLen], "[id=%d]", queryID);
			processQuery(&query[1], queryLen - 1, queryString);
		}
		else
			processQuery(query, queryLen, queryString);
	} // end while (fgets(line, sizeof(line), stdin) != NULL)

} // end of processPlain()


void processWumpus() {
	char line[32768];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if ((line[0] == 0) || (line[0] == '\n'))
			continue;
		line[strlen(line) - 1] = 0;
		processQuery(line);
	}
} // end of processWumpus()


int main(int argc, char **argv) {
	// process command-line parameter and check for errors
	if (argc < 5)
		printUsage();
	if ((strcasecmp(argv[argc - 1], "mode=trec") != 0) && (strcasecmp(argv[argc - 1], "mode=plain") != 0) &&
	    (strcasecmp(argv[argc - 1], "mode=wumpus") != 0) && (strcasecmp(argv[argc - 1], "mode=trec_anchor") != 0))
		printUsage();
	if (strncasecmp(argv[argc - 2], "n=", 2) != 0)
		printUsage();
	if (sscanf(&argv[argc - 2][2], "%d", &documentCount) != 1)
		printUsage();
	serverCount = (argc - 3) / 2;
	if (serverCount >= MAX_SERVER_COUNT) {
		fprintf(stderr, "Too many servers (maximum: %d).\n", MAX_SERVER_COUNT);
		exit(1);
	}
	for (int i = 0; i < serverCount; i++) {
		hostName[i] = argv[1 + 2 * i];
		portNumber[i] = atoi(argv[2 + 2 * i]);
	}

	// initialize character translation table
	translationTable[0] = 0;
	for (int i = 1; i < 128; i++)
		translationTable[i] = ' ';
	for (int i = 'a'; i <= 'z'; i++)
		translationTable[i] = translationTable[i - 32] = i;
	for (int i = '0'; i <= '9'; i++)
		translationTable[i] = i;

	connectToServers();

	if (strcasecmp(&argv[argc - 1][5], "trec") == 0)
		processTREC();
	else if (strcasecmp(&argv[argc - 1][5], "plain") == 0)
		processPlain();
	else
		processWumpus();

	closeServerConnections();

	return 0;
} // end of main()


