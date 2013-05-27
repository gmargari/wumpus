/**
 * This file contains the implementation for a TCP front-end to Wumpus. It reads
 * query logs from one or more input files and writes search results either to
 * stdout or to one or more output files. It can access one or more instances of
 * Wumpus through a TCP connection.
 *
 * By default, queries are read from stdin and search results are written to
 * stdout. The query format is assumed to be TREC format, and so is the output
 * format. Use the appropriate command-line parameters to change this behavior.
 *
 * If processing queries from more than one input stream, queries in each stream
 * will be processed sequentially. Queries from different streams can be processed
 * in an interleaved fashion.
 *
 * author: Stefan Buettcher
 * created: 2006-09-26
 * changed: 2007-11-23
 **/


#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <resolv.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <set>
#include <string>
#include "../misc/all.h"
#include "../misc/stopwords.h"
#include "../misc/language.h"


using namespace std;


static const int MAX_QUERY_LENGTH = 65536;

static const int MAX_FILE_COUNT = 32;

static int inputFileCount = 0;
static char *inputFileNames[MAX_FILE_COUNT];
static FILE *inputFiles[MAX_FILE_COUNT];
static int queriesProcessed[MAX_FILE_COUNT];
static bool inputFileBlocked[MAX_FILE_COUNT];

static int outputFileCount = 0;
static char *outputFileNames[MAX_FILE_COUNT];
static FILE *outputFiles[MAX_FILE_COUNT];

static const int INPUT_FORMAT_WUMPUS = 1;
static const int INPUT_FORMAT_PLAIN = 2;
static const int INPUT_FORMAT_TREC = 3;

static int inputFormat = INPUT_FORMAT_TREC;

static const int OUTPUT_FORMAT_TREC = 1;
static const int OUTPUT_FORMAT_SNIPPET = 2;

static int outputFormat = OUTPUT_FORMAT_TREC;
static int resultCount = 20;

static const int MAX_SERVER_COUNT = 32;

// if true, then we replace whitespace characters in docids by underscores
static const bool UNDERSCORE_DOCIDS = true;

static int serverCount = 0;
FILE *connections[MAX_SERVER_COUNT];

/**
 * Average delay between the arrival of two subsequent search queries. Can be
 * used to model a real-world environment with given query arrival rate.
 * Time gaps between subsequent queries will be exponentially distributed.
 **/
static int avgDelay = 0;
static long long nextQueryArrival = 0;

static bool stemming = true;

static bool stopwordRemoval = true;

/** TREC topic fields to use for query generation. **/
static int trecFieldCount = 0;
static const int MAX_TREC_FIELD_COUNT = 8;
static char *trecFields[MAX_TREC_FIELD_COUNT];

/** The search command to send to the Wumpus server(s). **/
static const char *wumpusCommand = "bm25";

static const char *retrievalUnit = "";

static const char *runID = "wumpus";

static char *userName = NULL, *password = NULL;

static bool logToStderr = false;

/** Synchronization for reading from input and writing to output. **/
static sem_t inputMutex;
static sem_t outputMutex;

static const int MAX_QUERYID_LENGTH = 31;
static const int MAX_DOCID_LENGTH = 255;
static const int MAX_RESULT_COUNT = 10000;

struct SearchResults {
	int count;
	char queryID[MAX_QUERYID_LENGTH + 1];
	char docIDs[MAX_RESULT_COUNT][MAX_DOCID_LENGTH + 1];
	double scores[MAX_RESULT_COUNT];
};


static void processQuery(const char *wumpusCommand, FILE *connection, SearchResults *results);


static void printUsage() {
	fprintf(stderr, "This is a Wumpus front-end program that can be used to send queries to Wumpus\n");
	fprintf(stderr, "servers running on the same machine or somewhere else.\n\n");
	fprintf(stderr, "Usage:  frontend --servers=HOST1:PORT2,HOST2:PORT2,... \\\n");
	fprintf(stderr, "           [--input=FILE1,FILE2,...] [--output=FILE1,FILE2,...] \\\n");
	fprintf(stderr, "           [--input_format=TREC|plain|wumpus] [--output_format=TREC|snippet] \\\n");
	fprintf(stderr, "           [--runid=RUN_ID] [--remove_stopwords=TRUE|false] \\\n");
	fprintf(stderr, "           [--avg_delay=MILLISECONDS] [--login=username:password] \\\n");
	fprintf(stderr, "           [--command=BM25|QAP|...] [--retrieval_unit=GCL_EXP(default:$DOCS)] \\\n");
	fprintf(stderr, "           [--count=INTEGER(default:20)] \\\n");
	fprintf(stderr, "           [--stemming=TRUE|false] [--trec_fields=TITLE,desc,...]\n\n");
	fprintf(stderr, "   If no input file is given, queries are read from stdin. If no output file is\n");
	fprintf(stderr, "given, results are written to stdout. The number of output files either has to\n");
	fprintf(stderr, "be 1 (or zero) or equal to the number of input files, in which case the results\n");
	fprintf(stderr, "to a query from input file N will be written to output file N.\n");
	fprintf(stderr, "   If multiple servers are specified, they will be used in parallel. However,\n");
	fprintf(stderr, "queries from the same input file will be processed sequentially. Thus, specify-\n");
	fprintf(stderr, "ing more servers than input files does not help.\n");
	fprintf(stderr, "   The avg_delay parameter can be used to specify a mean delay between the\n");
	fprintf(stderr, "arrival of two subsequent queries. Arrivals will then take place according to\n");
	fprintf(stderr, "an exponential distribution with the given mean.\n");
	fprintf(stderr, "   For the remaining parameters, the default value is indicated by upper-case\n");
	fprintf(stderr, "letters. To change the value, e.g. enable stemming, follow the syntax above.\n\n");
	exit(1);
} // end of printUsage()


static const int CASE_SENSITIVE = 1;
static const int CASE_INSENSITIVE = 2;

static bool startsWith(const char *s1, const char *s2, int caseSensitive) {
	int len1 = strlen(s1);
	int len2 = strlen(s2);
	if (len2 > len1)
		return false;
	else if (caseSensitive == CASE_INSENSITIVE)
		return (strncasecmp(s1, s2, len2) == 0);
	else if (caseSensitive == CASE_SENSITIVE)
		return (strncmp(s1, s2, len2) == 0);
	else {
		fprintf(stderr, "Illegal value for caseSensitive: %d\n", caseSensitive);
		exit(1);
	}
} // end of startsWith(const char*, const char*, int)


static long long currentDay = 0;
static long long prevMS = 999999999;
static long long getCurrentTimeMillis() {
	struct timeval t;
	gettimeofday(&t, NULL);
	long long ms = t.tv_sec * 1000 + t.tv_usec / 1000;
	if (ms < prevMS) {
		currentDay = time(NULL);
		currentDay = currentDay * 24 * 3600 * 1000;
	}
	prevMS = ms;
	return currentDay + ms;
} // end of getCurrentTimeMillis()


static void complainAndDie(const char *complaint, const char *details) {
	fprintf(stderr, "%s: %s\n", complaint, details);
	exit(1);
} // end of complainAndDie(const char*, const char*)


static void complainAndDie(const char *complaint) {
	fprintf(stderr, "%s\n", complaint);
	exit(1);
} // end of complainAndDie(const char*)


/**
 * Takes a string, splits it into its basic components, with a new component
 * beginning at each occurrence of the separator symbol, and puts the results
 * into the given array. Returns the number of components found. If "autotrim"
 * is set to true, it automatically trims whitespace characters in the basic
 * components from both sides.
 **/
static int splitIntoArray(const char *string, char separator, char *array[], bool autotrim) {
	assert(string != NULL);
	int result = 0;
	const char *nextSep;
	while ((nextSep = strchr(string, separator)) != NULL) {
		int len = (int)(nextSep - string);
		char *component = (char*)malloc(len + 1);
		strncpy(component, string, len);
		component[len] = 0;
		string = &nextSep[1];
		array[result++] = component;
	}
	strcpy(array[result++] = (char*)malloc(strlen(string) + 1), string);
	if (autotrim)
		for (int i = 0; i < result; i++)
			trimString(array[i]);
	return result;
} // end of splitIntoArray(const char*, char, char*[]);


/**
 * Opens a TCP connection to the given server at the given port. Returns a
 * FILE object that can be used to send/recv data to/from the server.
 **/
static FILE *connectToServer(const char *hostName, int port) {
	int s = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_aton(hostName, &addr.sin_addr);
	addr.sin_port = htons(port);
	if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
		char message[256];
		sprintf(message, "%s:%d", hostName, port);
		complainAndDie("Unable to connect to server", message);
	}
	FILE *result = fdopen(s, "a+");
	char dummy[1024];
	// read the first line (welcome line)
	if (fgets(dummy, sizeof(dummy), result) == NULL) dummy[0] = 0;
	return result;
} // end of connectToServer(const char*, int)


static void closeConnections() {
	for (int i = 0; i < serverCount; i++)
		fclose(connections[i]);
} // end of closeConnections()


/**
 * Processes the command-line parameters and updates internal configuration
 * variables accordingly.
 **/
static void processParameters(int argc, char **argv) {
	if (argc <= 1)
		printUsage();

	// by default, use TREC <title> field to generate query
	trecFields[0] = duplicateString("<title>");
	trecFieldCount = 1;

	for (int i = 1; i < argc; i++) {

		if (startsWith(argv[i], "--servers=", CASE_INSENSITIVE)) {
			if (strlen(argv[i]) > 256)
				complainAndDie("Argument too long", argv[i]);
			char *servers[128];
			serverCount = splitIntoArray(&argv[i][10], ',', servers, true);
			if (serverCount > MAX_SERVER_COUNT)
				complainAndDie("Too many servers.");
			for (int i = 0; i < serverCount; i++) {
				char *colon = strchr(servers[i], ':');
				if ((colon == NULL) || (colon == servers[i]))
					complainAndDie("Illegal server format (expected: HOSTNAME:PORT)", servers[i]);
				int port = 0;
				if (sscanf(&colon[1], "%d", &port) != 1)
					complainAndDie("Illegal port number", &colon[1]);
				*colon = 0;
				connections[i] = connectToServer(servers[i], port);
				assert(connections[i] != NULL);
				fprintf(stderr, "Connected to %s:%d\n", servers[i], port);
				free(servers[i]);
			}
		}

		if (startsWith(argv[i], "--stemming=", CASE_INSENSITIVE)) {
			if (strcasecmp(&argv[i][11], "true") == 0)
				stemming = true;
			else if (strcasecmp(&argv[i][11], "false") == 0)
				stemming = false;
			else
				complainAndDie("Illegal argument (\"true\" or \"false\" expected)", argv[i]);
		}

		if (startsWith(argv[i], "--count=", CASE_INSENSITIVE)) {
			char *p = &argv[i][8];
			if (sscanf(p, "%d", &resultCount) != 1)
				complainAndDie("Illegal argument (integer expected)", argv[i]);
			if (resultCount < 1)
				resultCount = 1;
			if (resultCount > MAX_RESULT_COUNT)
				resultCount = MAX_RESULT_COUNT;
		}

		if (startsWith(argv[i], "--login=", CASE_INSENSITIVE)) {
			char *p = &argv[i][8];
			if (strchr(p, ':') == NULL)
				complainAndDie("username:password expected", argv[i]);
			char *login = strcpy((char*)malloc(strlen(p) + 1), p);
			char *q = strchr(p, ':');
			*(q++) = 0;
			userName = strcpy((char*)malloc(strlen(p) + 1), p);
			password = strcpy((char*)malloc(strlen(q) + 1), q);
			free(login);
		}

		if (startsWith(argv[i], "--remove_stopwords=", CASE_INSENSITIVE)) {
			if (strcasecmp(&argv[i][19], "true") == 0)
				stopwordRemoval = true;
			else if (strcasecmp(&argv[i][19], "false") == 0)
				stopwordRemoval = false;
			else
				complainAndDie("Illegal argument (\"true\" or \"false\" expected)", argv[i]);
		}

		if (startsWith(argv[i], "--input=", CASE_INSENSITIVE)) {
			if (strlen(argv[i]) > 256)
				complainAndDie("Argument too long", argv[i]);
			char *inputs[128];
			inputFileCount = splitIntoArray(&argv[i][8], ',', inputs, true);
			if (inputFileCount > MAX_FILE_COUNT)
				complainAndDie("Too many input files.");
			for (int i = 0; i < inputFileCount; i++) {
				inputFileNames[i] = inputs[i];
				inputFiles[i] = NULL;
			}
		}

		if (startsWith(argv[i], "--output=", CASE_INSENSITIVE)) {
			if (strlen(argv[i]) > 256)
				complainAndDie("Argument too long", argv[i]);
			char *outputs[128];
			outputFileCount = splitIntoArray(&argv[i][9], ',', outputs, true);
			if (outputFileCount > MAX_FILE_COUNT)
				complainAndDie("Too many output files.");
			for (int i = 0; i < outputFileCount; i++) {
				outputFileNames[i] = outputs[i];
				outputFiles[i] = NULL;
			}
		}

		if (startsWith(argv[i], "--input_format=", CASE_INSENSITIVE)) {
			if (strcasecmp(&argv[i][15], "trec") == 0)
				inputFormat = INPUT_FORMAT_TREC;
			else if (strcasecmp(&argv[i][15], "plain") == 0)
				inputFormat = INPUT_FORMAT_PLAIN;
			else if (strcasecmp(&argv[i][15], "wumpus") == 0)
				inputFormat = INPUT_FORMAT_WUMPUS;
			else
				complainAndDie("Illegal argument", argv[i]);
		}

		if (startsWith(argv[i], "--avg_delay=", CASE_INSENSITIVE)) {
			if (sscanf(&argv[i][12], "%d", &avgDelay) != 1)
				complainAndDie("Not a number", &argv[i][12]);
			if (avgDelay < 0)
				complainAndDie("Not a valid delay", &argv[i][12]);
		}

		if (startsWith(argv[i], "--output_format=", CASE_INSENSITIVE)) {
			if (strcasecmp(&argv[i][16], "trec") == 0)
				outputFormat = OUTPUT_FORMAT_TREC;
			else if (strcasecmp(&argv[i][16], "snippet") == 0) {
				outputFormat = OUTPUT_FORMAT_SNIPPET;
				complainAndDie("Output format not supported yet", argv[i]);
			}
			else
				complainAndDie("Illegal argument", argv[i]);
		}

		if (startsWith(argv[i], "--runid=", CASE_INSENSITIVE)) {
			if (strlen(argv[i]) > 256)
				complainAndDie("Argument too long", argv[i]);
			runID = &argv[i][8];
		}

		if (startsWith(argv[i], "--command=", CASE_INSENSITIVE)) {
			if (strlen(argv[i]) > 256)
				complainAndDie("Argument too long", argv[i]);
			char *p = &argv[i][10];
			if (*p == '@')
				p++;
			wumpusCommand = p;
		}

		if (startsWith(argv[i], "--retrieval_unit=", CASE_INSENSITIVE)) {
			if (strlen(argv[i]) > 256)
				complainAndDie("Argument too long", argv[i]);
			retrievalUnit = &argv[i][17];
		}

		if (startsWith(argv[i], "--trec_fields=", CASE_INSENSITIVE)) {
			trecFieldCount = 0;
			StringTokenizer tok(&argv[i][14], ",");
			for (char *token = tok.nextToken(); token != NULL; token = tok.nextToken()) {
				char *field = duplicateString(token);
				toLowerCase(field);
				trecFields[trecFieldCount++] = concatenateStrings("<", field, ">");
				free(field);
			}
		}

		if (strcasecmp(argv[i], "--logtostderr") == 0)
			logToStderr = true;

		if ((strcasecmp(argv[i], "-h") == 0) || (strcasecmp(argv[i], "--help") == 0))
			printUsage();

	} // end for (int i = 1; i < argc; i++)

	// deal with exceptional situations
	if (serverCount == 0)
		complainAndDie("No servers specified.");
	if (inputFileCount == 0) {
		fprintf(stderr, "No input file specified. Assuming stdin.\n");
		inputFiles[inputFileCount++] = stdin;
	}
	if (outputFileCount == 0) {
		fprintf(stderr, "No output file specified. Assuming stdout.\n");
		outputFiles[outputFileCount++] = stdout;
	}
	if ((outputFileCount != 1) && (outputFileCount != inputFileCount))
		complainAndDie("Number of output files must match number of input files.");

	// open input files
	for (int i = 0; i < inputFileCount; i++) {
		if (inputFiles[i] != NULL)
			continue;
		inputFiles[i] = fopen(inputFileNames[i], "r");
		if (inputFiles[i] == NULL)
			complainAndDie("Unable to open input file", inputFileNames[i]);
		queriesProcessed[i] = 0;
		inputFileBlocked[i] = false;
	}

	// open output files; truncate to zero if already existing
	for (int i = 0; i < outputFileCount; i++) {
		if (outputFiles[i] != NULL)
			continue;
		outputFiles[i] = fopen(outputFileNames[i], "w");
		if (outputFiles[i] == NULL)
			complainAndDie("Unable to create output file", outputFileNames[i]);
	}


	// if a username:password pair was given ("--login=..."), then we login to
	// every server at this point
	if (userName != NULL) {
		char *line = (char*)malloc(strlen(userName) + strlen(password) + 32);
		sprintf(line, "@login %s %s", userName, password);
		SearchResults results;
		for (int i = 0; i < serverCount; i++)
			processQuery(line, connections[i], &results);
	}
} // end of processParameters(int, char**)


/**
 * Takes a Wumpus query, forwards it to the server given by "connection", collects
 * the search results, and puts them into the buffer given by "results".
 **/
static void processQuery(const char *wumpusCommand, FILE *connection, SearchResults *results) {
	if (logToStderr)
		fprintf(stderr, "%s\n", wumpusCommand);
	fprintf(connection, "%s\n", wumpusCommand);
	fflush(connection);

	char line[1024], queryID[1024], dummy[1024], docID[1024];
	results->queryID[0] = 0;
	results->count = 0;
	while (fgets(line, sizeof(line), connection) != NULL) {
		int lineLen = strlen(line);
		if (lineLen <= 1)
			continue;
		if (line[lineLen - 1] == '\n')
			line[--lineLen] = 0;
		if (line[0] == '@') {
			if (logToStderr)
				fprintf(stderr, "%s\n", line);
			break;
		}
		sscanf(line, "%s%lf%s%s%s", queryID, &results->scores[results->count], dummy, dummy, docID);
		if (results->queryID[0] == 0)
			strcpy(results->queryID, queryID);
		else if (strcmp(queryID, results->queryID) != 0) {
			char ids[256];
			sprintf(ids, "%s <=> %s", queryID, results->queryID);
			complainAndDie("Inconsistent query IDs from server", ids);
		}

		// process docid
		if ((docID[0] == '<') || (strncmp(docID, "\"<", 2) == 0)) {
			// crap! docid is in some special bogus format; remove XML tags from front and end
			char *ptr = strstr(line, docID);
			assert(ptr != NULL);
			if (strchr(ptr, '>') != NULL) {
				ptr = strchr(ptr, '>') + 1;
				if (strchr(ptr, '<') != NULL) {
					*strchr(ptr, '<') = 0;
					strcpy(docID, ptr);
				}
			}
		}
		replaceChar(docID, '"', ' ', true);
		trimString(docID);
		if (strlen(docID) > (unsigned int)MAX_DOCID_LENGTH)
			docID[MAX_DOCID_LENGTH] = 0;
		if (UNDERSCORE_DOCIDS)
			replaceChar(docID, ' ', '_', true);
		strcpy(results->docIDs[results->count], docID);
		results->count++;
	}

	if (results->queryID[0] == 0) {
		const char *id = strstr(wumpusCommand, "[id=");
		if (id != NULL) {
			id += 4;
			char *end = const_cast<char*>(strchr(id, ']'));
			if (end != NULL) {
				*end = 0;
				strcpy(results->queryID, id);
				*end = ']';
			}
		}
	}
} // end of processQuery(const char*, FILE*, SearchResults*)


/**
 * Prints the given search results to the output file specified by "file".
 * The format of the output depends on the value of the global variable
 * "outputFormat".
 **/
static void printResults(FILE *file, SearchResults *results, int latency) {
	for (int i = 0; i < results->count; i++) {
		switch (outputFormat) {
			case OUTPUT_FORMAT_TREC:
				fprintf(file, "%s Q0 %s %d %.4lf %s\n",
						   results->queryID, results->docIDs[i], i + 1, results->scores[i], runID);
				break;
			default:
				fprintf(stderr, "Output format not implemented: %d\n", outputFormat);
				exit(1);
		}
	}
	if (logToStderr) {
		if (results->count == 0) {
			fprintf(stderr, "@0-Frontend. Query ID: %s. Latency: %d ms.\n",
			        results->queryID, latency);
		}
		else {
			fprintf(stderr, "@0-Frontend. Query ID: %s. Latency: %d ms. Results found: %d.\n",
			        results->queryID, latency, results->count);
		}
	}
} // end of printResults(FILE*, SearchResults*, int)


static void eliminateStopwords(map<string,double> *queryTerms) {
	set<string> toRemove;
	for (map<string,double>::iterator iter = queryTerms->begin(); iter != queryTerms->end(); ++iter)
		if (isStopword(iter->first.c_str(), LANGUAGE_ENGLISH))
			toRemove.insert(iter->first);
	for (set<string>::iterator iter = toRemove.begin(); iter != toRemove.end(); ++iter)
		queryTerms->erase(*iter);
} // end of eliminateStopwords(map<string,double>*)


static void rawTermsToWumpus(map<string,double> *queryTerms, char *queryID, char *queryString) {
	if (stopwordRemoval)
		eliminateStopwords(queryTerms);

	int queryLen =
		sprintf(queryString, "@%s[id=%s][count=%d][docid] ", wumpusCommand, queryID, resultCount);
	if (strlen(retrievalUnit) > 0)
		queryLen += sprintf(&queryString[queryLen], "%s by ", retrievalUnit);

	map<string,double>::iterator iter;
	for (iter = queryTerms->begin(); iter != queryTerms->end(); ++iter) {
		if (iter != queryTerms->begin())
			queryLen += sprintf(&queryString[queryLen], ", ");
		queryLen += sprintf(&queryString[queryLen], "#%.4lf \"%s%s\"",
				                iter->second, (stemming ? "$" : ""), iter->first.c_str());
	}
} // end of rawTermsToWumpus(map<string,double>*, char*)


static bool charTableInitialized = false;
char charTable[256];


static void initializeCharTable() {
	sem_wait(&outputMutex);
	for (int i = 0; i < 256; i++) {
		if ((i == 0) || (i >= 128))
			charTable[i] = i;
		else if ((i >= '0') && (i <= '9'))
			charTable[i] = i;
		else if ((i >= 'A') && (i <= 'Z'))
			charTable[i] = (i | 32);
		else if ((i >= 'a') && (i <= 'z'))
			charTable[i] = i;
		else
			charTable[i] = ' ';
	}
	charTableInitialized = true;
	sem_post(&outputMutex);
} // end of initializeCharTable()


static void plainToWumpus(char *queryString) {
	if (!charTableInitialized)
		initializeCharTable();
	for (int i = 0; queryString[i] != 0; i++)
		queryString[i] = charTable[(unsigned char)queryString[i]];
	while ((*queryString > 0) && (*queryString <= ' '))
		queryString++;
	
	int queryID = -1;
	char *q = queryString;
	if (sscanf(queryString, "%d", &queryID) != 1)
		queryID = -1;
	else
		while (*q > ' ') q++;

	// tokenize the line and build a map from query terms to their weights
	map<string,double> queryTerms;
	char *terms[MAX_QUERY_LENGTH];
	int termCount = splitIntoArray(q, ' ', terms, true);
	for (int i = 0; i < termCount; i++) {
		char *term = terms[i];
		if (term[0] != 0) {
			if (queryTerms.find(term) == queryTerms.end())
				queryTerms[term] = 1;
			else
				queryTerms[term] += 1;
		}
		free(term);
	}

	char sQueryID[32];
	if (queryID >= 0)
		sprintf(sQueryID, "%d", queryID);
	else
		strcpy(sQueryID, "0");
	rawTermsToWumpus(&queryTerms, sQueryID, queryString);
} // end of plainToWumpus(char*)


void trecToWumpus(char *trecTopic) {
	if (!charTableInitialized)
		initializeCharTable();

	// extract query ID
	char sQueryID[32];
	char *id = strcasestr(trecTopic, "<id>");
	if (id == NULL)
		id = strcasestr(trecTopic, "<qid>");
	if (id == NULL)
		id = strcasestr(trecTopic, "<num>");
	if (id == NULL) {
		trecTopic[0] = 0;
		return;
	}
	id = strchr(id, '>') + 1;
	StringTokenizer tok(id, " \t\r\n");
	for (char *token = tok.nextToken(); token != NULL; token = tok.nextToken()) {
		if ((token[0] != 0) && (strcasecmp(token, "Number:") != 0)) {
			snprintf(sQueryID, sizeof(sQueryID), "%s", token);
			sQueryID[sizeof(sQueryID) - 1] = 0;
			break;
		}
	}
	if (strchr(sQueryID, '<') != NULL)
		*strchr(sQueryID, '<') = 0;

	// iterate over all fields specified by user and add their contents to the query
	map<string,double> queryTerms;
	for (int i = 0; i < trecFieldCount; i++) {
		char *fieldStart = strcasestr(trecTopic, trecFields[i]);
		if (fieldStart == NULL)
			continue;
		fieldStart += strlen(trecFields[i]);
		while ((*fieldStart > 0) && (*fieldStart <= ' '))
			fieldStart++;
		if (startsWith(fieldStart, "Description:"))
			fieldStart += 12;
		if (startsWith(fieldStart, "Narrative:"))
			fieldStart += 10;
		char *fieldEnd = strchr(fieldStart, '<');
		if (fieldEnd != NULL)
			*fieldEnd = 0;

		for (int k = 0; fieldStart[k] != 0; k++)
			fieldStart[k] = charTable[(unsigned char)fieldStart[k]];

		char *terms[MAX_QUERY_LENGTH];
		int termCount = splitIntoArray(fieldStart, ' ', terms, true);
		for (int k = 0; k < termCount; k++) {
			char *term = terms[k];
			if (term[0] != 0) {
				if (queryTerms.find(term) == queryTerms.end())
					queryTerms[term] = 1;
				else
					queryTerms[term] += 1;
			}
			free(term);
		}

		if (fieldEnd != NULL)
			*fieldEnd = '<';
	} // end for (int i = 0; i < trecFieldCount; i++)

	rawTermsToWumpus(&queryTerms, sQueryID, trecTopic);
} // end of trecToWumpus(char*)


/**
 * Fetches a new query from one of the query streams, transforms it into Wumpus
 * format and puts it into the buffer given by "queryString".
 **/
static bool fetchNewQuery(char *queryString, int *inputStreamID, long long *arrivalTime) {
	int inputStream = -1, len = 0;
	for (int i = 0; i < inputFileCount; i++)
		if (!inputFileBlocked[i]) {
			if (inputStream < 0)
				inputStream = i;
			else if (queriesProcessed[i] < queriesProcessed[inputStream])
				inputStream = i;
		}
	if (inputStream < 0)
		return false;
	*inputStreamID = inputStream;

	switch (inputFormat) {
		case INPUT_FORMAT_WUMPUS:
			queryString[0] = 0;
			while (strlen(queryString) <= 1) {
				if (fgets(queryString, 1024, inputFiles[inputStream]) == NULL) {
					inputFileBlocked[inputStream] = true;
					return fetchNewQuery(queryString, inputStreamID, arrivalTime);
				}
				trimString(queryString);
			}
			break;
		case INPUT_FORMAT_PLAIN:
			queryString[0] = 0;
			while (strlen(queryString) <= 1) {
				if (fgets(queryString, 1024, inputFiles[inputStream]) == NULL) {
					inputFileBlocked[inputStream] = true;
					return fetchNewQuery(queryString, inputStreamID, arrivalTime);
				}
				trimString(queryString);
			}
			plainToWumpus(queryString);
			break;
		case INPUT_FORMAT_TREC:
			queryString[len = 0] = 0;
			while (len < MAX_QUERY_LENGTH) {
				int c = fgetc(inputFiles[inputStream]);
				if (c == EOF)
					break;
				queryString[len++] = c;
				if (c == '>') {
					if ((endsWith(queryString, len, "<top>", 5, false)) ||
							(endsWith(queryString, len, "<topic>", 7, false))) {
						len = sprintf(queryString, "%s", "<top>");
						continue;
					}
					if ((endsWith(queryString, len, "</top>", 6, false)) ||
							(endsWith(queryString, len, "</topic>", 8, false))) {
						break;
					}
				}
			}
			queryString[len] = 0;
			trecToWumpus(queryString);
			if (queryString[0] == 0)
				return false;
			break;
		default:
			assert(false);
	}

	if (strlen(queryString) > 1) {
		queriesProcessed[inputStream]++;

		if (avgDelay <= 0) {
			*arrivalTime = getCurrentTimeMillis();
		}
		else {
			// wait until next query arrives
			long long currentTime;
			while ((currentTime = getCurrentTimeMillis()) < nextQueryArrival)
				usleep((nextQueryArrival - currentTime) * 1000);

			// model exponential distribution in arrival time gaps
			double delta = (random() % (1 << 30)) / (1.0 * (1 << 30));
			double delay = avgDelay * (-log(1 - delta));
			*arrivalTime = nextQueryArrival;
			nextQueryArrival = *arrivalTime + (int)(delay + 0.5);
		}

		return true;
	}
	else
		return fetchNewQuery(queryString, inputStreamID, arrivalTime);
} // end of fetchNewQuery(char*, int*)


/**
 * In a loop, fetches queries from the input streams, forwards them to the
 * server and prints search results to the specified output stream(s).
 **/
static void *consumeQueries(void *data) {
	char queryString[MAX_QUERY_LENGTH + 1];
	int connectionID = *((int*)data);
	SearchResults results;
	int inputStreamID;
	while (true) {
		sem_wait(&inputMutex);
		long long queryArrivalTime;
		bool status = fetchNewQuery(queryString, &inputStreamID, &queryArrivalTime);
//		inputFileBlocked[inputStreamID] = true;
		sem_post(&inputMutex);
		if (!status)
			break;
		processQuery(queryString, connections[connectionID], &results);
		sem_wait(&outputMutex);
		printResults(outputFiles[outputFileCount == 1 ? 0 : connectionID], &results,
				         getCurrentTimeMillis() - queryArrivalTime);
//		inputFileBlocked[inputStreamID] = false;
		sem_post(&outputMutex);
	}
	return NULL;
} // end of consumeQueries(void*)


/**
 * Starts a bunch of threads, each responsible for one Wumpus server, and waits
 * for them to terminate. Each thread will execute the loop in consumeQueries
 * until there are no more queries to process.
 **/
static void processQueries() {
	pthread_t threads[MAX_SERVER_COUNT];
	int connectionIDs[MAX_SERVER_COUNT];
	nextQueryArrival = getCurrentTimeMillis();
	for (int i = 0; i < serverCount; i++) {
		connectionIDs[i] = i;
		pthread_create(&threads[i], NULL, consumeQueries, &connectionIDs[i]);
	}
	for (int i = 0; i < serverCount; i++) {
		void *dummy;
		pthread_join(threads[i], &dummy);
	}
} // end of processQueries()


int main(int argc, char **argv) {
	processParameters(argc, argv);
	sem_init(&inputMutex, 0, 1);
	sem_init(&outputMutex, 0, 1);
	processQueries();
	closeConnections();
	for (int i = 0; i < outputFileCount; i++)
		if (outputFiles[i] != stdout)
			fclose(outputFiles[i]);
	sem_destroy(&inputMutex);
	sem_destroy(&outputMutex);
	return 0;
} // end of main(int, char**)


