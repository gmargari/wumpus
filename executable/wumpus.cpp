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
 * author: Stefan Buettcher
 * created: 2004-10-21
 * changed: 2006-07-08
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../extentlist/extentlist.h"
#include "../index/index.h"
#include "../masterindex/masterindex.h"
#include "../misc/all.h"
#include "../query/query.h"


#define PRINT_DEBUG_INFORMATION 1

static char workDir[MAX_CONFIG_VALUE_LENGTH + 32];

static int statusCode;

static char responseLine[Query::MAX_RESPONSELINE_LENGTH];

static Index *myIndex = NULL;


static void printHelp() {
	printf("Syntax: wumpus [--KEY=VALUE]\n\n");
	printf("KEY and VALUE can be arbitrary index configuration pairs. Give \"CONFIGURATION\"\n");
	printf("as KEY in order to process the configuration file given by VALUE.\n");
	printf("The index directory is specified using --DIRECTORY=...\n\n");
	exit(0);
} // end of printHelp()


static void processParameter(char *p) {
	if ((strcasecmp(p, "--help") == 0) || (strcasecmp(p, "-h") == 0))
		printHelp();
} // end of processParameter(char*)


static void printExecutionStatistics() {
	long long e, t;
	char buffer[256];
	getExecutionStatistics(&e, &t);
	sprintf(buffer, "External commands executed: %lld. Total time spent: %lld ms.", e, t);
	log(LOG_DEBUG, "Execute", buffer);
} // end of printExecutionStatistics()


static void printReadWriteStatistics() {
	long long r, w;
	char buffer[256];
	getReadWriteStatistics(&r, &w);
	sprintf(buffer, "Bytes read: %lld. Bytes written: %lld.", r, w);
	log(LOG_DEBUG, "IO", buffer);
} // end of printReadWriteStatistics()


static void processSequence(char *fileName, Index *index) {
	FILE *f = fopen(fileName, "r");
	if (f == NULL)
		printf("@1-No such file.\n");
	else {
		char line[8192];
		while (fgets(line, sizeof(line), f) != NULL) {
			if (strlen(line) > 1)
				line[strlen(line) - 1] = 0;
			Query *q = new Query(index, line, getuid());
			q->parse();
			while (q->getNextLine(responseLine))
				printf("%s\n", responseLine);
			q->getStatus(&statusCode, responseLine);
			delete q;
			printf("@%d-%s\n", statusCode, responseLine);
		}
		fclose(f);
	}
} // end of processSequence(char*, Index*)


static void runFromDevNull() {
	while (true)
		waitMilliSeconds(1000);
} // end of runFromDevNull()


static void runFromStdin() {
	char buffer[65536];
	char *lineRead;

	// print copyright notice etc.
	Query *q = new Query(myIndex, "@about", getuid());
	q->parse();
	while (q->getNextLine(responseLine))
		fprintf(stderr, "%s\n", responseLine);
	delete q;
	
	printf("@0-Index loaded. Enter @exit or ^D to end the session.\n");
	while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
		lineRead = chop(buffer);
		char *line = lineRead;
		while ((*line > 0) && (*line <= ' '))
			line++;

		// Print debug output for the current line.
		log(LOG_DEBUG, "wumpus.cpp", line);

		if (line[0] == 0) {
			free(lineRead);
			printf("@1-Empty query. (0 ms)\n");
			continue;
		}

		if ((strcasecmp(line, "@exit") == 0) || (strcasecmp(line, "@quit") == 0))
			break;

		char command[8192], argument[8192];
		if (startsWith(line, "@sequence "))
			processSequence(&line[strlen("@sequence ")], myIndex);
		else {
			sscanf(line, "%s", command);
			strcpy(argument, &line[strlen(command) + 1]);
			Query *q = new Query(myIndex, line, getuid());
			q->parse();
			while (q->getNextLine(responseLine))
				printf("%s\n", responseLine);
			q->getStatus(&statusCode, responseLine);
			delete q;
			printf("@%d-%s\n", statusCode, responseLine);
		}
		free(lineRead);
	}

	log(LOG_DEBUG, "wumpus.cpp", "End of input reached.");
} // end of runFromStdin()


int main(int argc, char **argv) {
	initializeConfiguratorFromCommandLineParameters(argc, (const char**)argv);
	log(LOG_DEBUG, "Index", "Starting application.");
	for (int i = 1; i < argc; i++)
		processParameter(argv[i]);

	// make sure the index directory has been specified (either in the config file
	// or at the command line)
	if (!getConfigurationValue("DIRECTORY", workDir)) {
		fprintf(stderr, "ERROR: No directory specified. Check .wumpusconf file or give directory as command-line parameter.\n\n");
		exit(1);
	}

	// check whether we have multiple index directories; if this is the case, we
	// have to create a MasterIndex instead of a normal Index instance
	if (strchr(workDir, ',') != NULL) {
		char *dirs[100];
		int indexCount = 0;
		StringTokenizer *tok = new StringTokenizer(workDir, ",");
		while (tok->hasNext()) {
			char *token = tok->getNext();
			if (token[strlen(token) - 1] == '/')
				dirs[indexCount++] = duplicateString(token);
			else
				dirs[indexCount++] = concatenateStrings(token, "/");
			if (indexCount >= 100)
				break;
		}
		delete tok;
		myIndex = new MasterIndex(indexCount, dirs);
		for (int i = 0; i < indexCount; i++)
			free(dirs[i]);
	} // end if (strchr(workDir, ',') != NULL)
	else {
		if (workDir[strlen(workDir) - 1] != '/')
			strcat(workDir, "/");
		myIndex = new Index(workDir, false);
	} // end else [ordinary Index instance, no MasterIndex]

	bool runAsDaemon;
	getConfigurationBool("RUN_AS_DAEMON", &runAsDaemon, false);
	if (runAsDaemon)
		runFromDevNull();
	else
		runFromStdin();

	// shutdown
	delete myIndex;

	if (PRINT_DEBUG_INFORMATION) {
		printExecutionStatistics();
		printReadWriteStatistics();
		printAllocations();
	}
	return 0;
} // end of main()



