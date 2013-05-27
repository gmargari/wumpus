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
 * This is Wumpus' TREC frontend. It can be used to index TREC-style document
 * collections and to run ad-hoc document retrieval tasks on the data.
 *
 * Run without command-line parameters in order to see a usage text.
 *
 * author: Stefan Buettcher
 * created: 2006-01-04
 * changed: 2006-05-04
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include "../config/config.h"
#include "../index/index.h"
#include "../misc/all.h"
#include "../query/query.h"
#include "../terabyte/terabyte_query.h"


#define RUNMODE_INDEX 1
#define RUNMODE_QUERY 2

#define CONFIG_FILE "wumpus.cfg"

#undef byte
#define byte unsigned char

static const char *LOG_ID = "TREC-Frontend";

static const char *RUN_ID = "Wumpus-TREC";

static int runMode;
static FILE *inputFile;
static FILE *outputFile;
static FILE *logFile;

static const char WHITESPACES[40] = {
	',', ';', '.', ':', '-', '_', '#', '\'', '+', '*', '~',
	'°', '^', '!', '"', '§', '$', '%', '&', '/', '(', ')',
	'[', ']', '{', '}', '=', '?', '\\', '<', '>', '|', 0
};

static char isWhiteSpace[256];

static const char *STOPWORDS[256] = {
	"",
	"a", "about", "are", "also", "and", "any", "as",
	"be", "been", "but", "by",
	"did", "does",
	"for",
	"had", "has", "have", "how",
	"etc",
	"if", "in", "is", "it", "its",
	"not",
	"of", "on", "or",
	"s", "so", "such",
	"than", "that", "the", "their", "there", "this", "then", "to",
	"was", "were", "what", "which", "who", "will", "with", "would",
	NULL
};

static const int HASHTABLE_SIZE = 7951;

static const char *STOPWORD_HASHTABLE[HASHTABLE_SIZE];

static Index *myIndex;
static char queryID[1024];


/** Prints a usage text to stderr and terminates the program. **/
static void usage() {
	fprintf(stderr, "Usage:  trec (INDEX|QUERY) INPUT_FILE OUTPUT_FILE LOG_FILE\n\n");
	fprintf(stderr, "In INDEX mode, the INPUT_FILE contains a list of input files for which an\n");
	fprintf(stderr, "index should be created (one file per line).\n");
	fprintf(stderr, "In QUERY mode, the INPUT_FILE contains a list of flat search queries, one\n");
	fprintf(stderr, "per line, of the form: \"TOPIC_ID TERM_1 TERM_2 ... TERM_N\"\n\n");
	fprintf(stderr, "OUTPUT_FILE and LOG_FILE will contain the output data produced by Wumpus.\n\n");
	exit(1);
} // end of usage()


/** Prints a message to stderr and terminates the program. **/
static void dieWithMessage(const char *s1, const char *s2) {
	fprintf(stderr, s1, s2);
	exit(1);
} // end of dieWithMessage(char*, char*, char*)


/** Processes the command-line parameters and makes sure all files exist etc. **/
static void processParameters(int argc, char **argv) {
	if (strcasecmp(argv[1], "index") == 0)
		runMode = RUNMODE_INDEX;
	else if (strcasecmp(argv[1], "query") == 0)
		runMode = RUNMODE_QUERY;
	else
		dieWithMessage("Illegal run mode: %s\n", argv[1]);
	inputFile = fopen(argv[2], "r");
	if (inputFile == NULL)
		dieWithMessage("Input file does not exist: %s\n", argv[2]);
	outputFile = fopen(argv[3], "a");
	if (outputFile == NULL)
		dieWithMessage("Unable to create output file: %s\n", argv[3]);
	fclose(outputFile);
	logFile = fopen(argv[4], "a");
	if (logFile == NULL)
		dieWithMessage("Unable to create log file: %s\n", argv[4]);
	fclose(logFile);

	fprintf(stderr, "Starting execution. Everything will be logged to \"%s\" and \"%s\".\n",
			argv[3], argv[4]);
	fprintf(stderr, "All data will be appended at the end of the respective file.\n\n");

	stdout = freopen(argv[3], "a", stdout);
	assert(stdout != NULL);
	stderr = freopen(argv[4], "a", stderr);
	assert(stderr != NULL);
} // end of processParameters(int, char**)


/** Loads the configuration file and initialized the configuration service. **/
static void initConfig() {
	static const char *CONFIG[20] = {
		"LOG_LEVEL=2",
		"LOG_FILE=stderr",
		"STEMMING_LEVEL=3",
		"MERGE_AT_EXIT=true",
		"MAX_FILE_SIZE=3000M",
		"MAX_UPDATE_SPACE=240M",
		"UPDATE_STRATEGY=NO_MERGE",
		"DOCUMENT_LEVEL_INDEXING=2",
		"COMPRESSED_INDEXCACHE=true",
		"POSITIONLESS_INDEXING=true",
		"LEXICON_TYPE=TERABYTE_LEXICON",
		"HYBRID_INDEX_MAINTENANCE=false",
		"APPLY_SECURITY_RESTRICTIONS=false",
		"CACHED_EXPRESSIONS=\"<doc>\"..\"</doc>\"",
		NULL
	};
	int configCnt = 0;
	while (CONFIG[configCnt] != NULL)
		configCnt++;
	initializeConfiguratorFromCommandLineParameters(configCnt, CONFIG);
	struct stat buf;
	if (stat(CONFIG_FILE, &buf) != 0)
		dieWithMessage("Unable to open configuration file: %s\n", CONFIG_FILE);
	initializeConfigurator(CONFIG_FILE, NULL);

	memset(isWhiteSpace, 0, sizeof(isWhiteSpace));
	for (int i = 1; i <= 32; i++)
		isWhiteSpace[i] = 1;
	for (int i = 0; WHITESPACES[i] != 0; i++)
		isWhiteSpace[(byte)WHITESPACES[i]] = 1;

	memset(STOPWORD_HASHTABLE, 0, sizeof(STOPWORD_HASHTABLE));
	for (int i = 0; STOPWORDS[i] != NULL; i++) {
		int slot = simpleHashFunction(STOPWORDS[i]) % HASHTABLE_SIZE;
		if (STOPWORD_HASHTABLE[slot] != NULL)
			fprintf(stderr, "%s <-> %s\n", STOPWORDS[i], STOPWORD_HASHTABLE[slot]);
		assert(STOPWORD_HASHTABLE[slot] == NULL);
		STOPWORD_HASHTABLE[slot] = STOPWORDS[i];
	}
} // end of initConfig()


/**
 * Returns true iff the given term is found in the list of stopwords, as defined
 * by the STOPWORDS array. STOPWORD_HASHTABLE is used to speed up the search
 * operation.
 **/
static bool isStopWord(char *term) {
	int hashSlot = simpleHashFunction(term) % HASHTABLE_SIZE;
	if (STOPWORD_HASHTABLE[hashSlot] == NULL)
		return false;
	else
		return (strcmp(term, STOPWORD_HASHTABLE[hashSlot]) == 0);
} // end of isStopWord(char*)


/** Prints the current timestamp to the log file (LOG_OUTPUT). **/
static void logTimeStamp() {
	char msg[64];
	struct timeval tv;
	gettimeofday(&tv, NULL);
	sprintf(msg, "Timestamp: %d.%03d", (int)tv.tv_sec, (int)(tv.tv_usec / 1000));
	log(LOG_OUTPUT, LOG_ID, msg);
} // end of logTimeStamp()


/**
 * Reads file names from the input file. For each file name, the file is added
 * to the index.
 **/
static void buildIndex() {
	char line[8192];
	log(LOG_OUTPUT, LOG_ID, "Building index.");
	logTimeStamp();

	myIndex = new Index("./database", false);
	int cnt = 0;
	while (fgets(line, sizeof(line), inputFile) != NULL) {
		char *fileName = chop(line);
		if (strlen(fileName) > 0) {
			myIndex->addFile(fileName, NULL);
			cnt++;
		}
		free(fileName);
	}
	delete myIndex;

	sprintf(line, "%d files indexed. Done.", cnt);
	log(LOG_OUTPUT, LOG_ID, line);
	logTimeStamp();
} // end of buildIndex()


static void *processQuery(void *data) {
	sched_yield();
	TerabyteQuery *tq = (TerabyteQuery*)data;
	char *queryString = tq->getQueryString();
	char line[4096];
	if (tq->parse()) {
		sprintf(line, "Processing query: %s", queryString);
		log(LOG_OUTPUT, LOG_ID, line);
		int rank = 0;
		while (tq->getNextLine(line)) {
			char dummy[256], docID[256];
			double score;
			rank++;
			sscanf(line, "%s%lf%s%s%s", dummy, &score, dummy, dummy, docID);
			docID[strlen(docID) - 1] = 0;
			printf("%s Q0 %s %d %.3lf %s\n", queryID, &docID[1], rank, score, RUN_ID);
		}
	}
	else {
		sprintf(line, "Unable to parse query: %s", queryString);
		log(LOG_ERROR, LOG_ID, line);
	}
	free(queryString);
	delete tq;
	return 0;
}


/**
 * Reads flat search queries of the form "TOPIC_ID TERM_1 .. TERM_N" from the
 * input file. For each query, a TerabyteQuery instance is created and executed.
 * The results are put into TREC format and printed to the output file.
 **/
void processQueries() {
	char line[8192], newQueryID[1024];
	
	myIndex = new Index("./database", false);

	// execute one @bm25tera query in order to initialize all internal data structures
	log(LOG_OUTPUT, LOG_ID, "Initializing TerabyteQuery cache data structures.");
	logTimeStamp();
	Query *iq = new Query(myIndex, "@bm25tera[docid] \"wumpus\"", Index::GOD);
	iq->parse();
	while (iq->getNextLine(line));
	delete iq;
	log(LOG_OUTPUT, LOG_ID, "Initialization finished.");
	logTimeStamp();
	
	int cnt = 0;
	log(LOG_OUTPUT, LOG_ID, "Starting to process search queries.");
	logTimeStamp();

	bool cpuRunning = false;
	pthread_t cpuThread;
	fflush(stdout);
	fflush(stderr);

	while (fgets(line, sizeof(line), inputFile) != NULL) {
		char *query = chop(line);
		char queryString[4096];
		queryString[0] = 0;
		
		if (strlen(query) <= 0)
			continue;

		int queryTermCount = 0;
		ExtentList *queryTerms[256];

		int len = sprintf(queryString, "");
		for (int i = 0; query[i] != 0; i++)
			if (query[i] > 0) {
				if (isWhiteSpace[query[i]])
					query[i] = ' ';
				else if ((query[i] >= 'A') && (query[i] <= 'Z'))
					query[i] += 32;
			}
		StringTokenizer *tok = new StringTokenizer(query, " ");
		strcpy(newQueryID, tok->getNext());
		while (tok->hasNext()) {
			char *token = tok->getNext();
			if ((!isStopWord(token)) && (strlen(token) + 3 <= MAX_TOKEN_LENGTH)) {
				len += sprintf(&queryString[len], "\"%s\"", token);
				if (tok->hasNext())
					len += sprintf(&queryString[len], ", ");
				char docLevelToken[256];
				sprintf(docLevelToken, "<!>%s", token);
				queryTerms[queryTermCount++] = myIndex->getPostings(docLevelToken, Index::GOD, true, false);
			}
		}
		delete tok;

		if (queryTermCount == 0)
			continue;

		const char *modifiers[3] = { "docid", "b=0.5", NULL };
		TerabyteQuery *tq =
			new TerabyteQuery(myIndex, "bm25tera", modifiers, queryString, (VisibleExtents*)NULL, -1);
#if 1
		assert(queryTermCount > 0);
		tq->setScorers(queryTerms, queryTermCount);
#else
		for (int i = 0; i < queryTermCount; i++)
			delete queryTerms[i];
#endif
#if 0
		if (cpuRunning) {
			pthread_join(cpuThread, NULL);
			cpuRunning = false;
		}
		strcpy(queryID, newQueryID);
		pthread_create(&cpuThread, NULL, processQuery, tq);
		cpuRunning = true;
#else
		strcpy(queryID, newQueryID);
		processQuery(tq);
#endif

		cnt++;
		free(query);
	}

	if (cpuRunning) {
		pthread_join(cpuThread, NULL);
		cpuRunning = false;
	}

	sprintf(line, "%d queries processed. Done.", cnt);
	log(LOG_OUTPUT, LOG_ID, line);
	logTimeStamp();
} // end of processQueries()


/** This is a main method. **/
int main(int argc, char **argv) {
	if (argc != 5)
		usage();
	processParameters(argc, argv);
	initConfig();
	if (runMode == RUNMODE_INDEX)
		buildIndex();
	else
		processQueries();
	long long bytesRead, bytesWritten;
	getReadWriteStatistics(&bytesRead, &bytesWritten);
	long long br = bytesRead, bw = bytesWritten;
	char message[256];
	sprintf(message, "Bytes read: %lld. Bytes written: %lld.", br, bw);
	log(LOG_OUTPUT, LOG_ID, message);
	return 0;
} // end of main(int, char**)



