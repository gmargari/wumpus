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
 * This is the front-end program to the index daemon. Communication with the
 * indexing service is realized by writing data to the service's FIFO,
 * and reading response data from another FIFO.
 *
 * author: Stefan Buettcher
 * created: 2004-10-15
 * changed: 2005-03-21
 **/


#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "../daemons/authconn_daemon.h"
#include "../index/index.h"
#include "../misc/configurator.h"
#include "../misc/utils.h"
#include "../misc/alloc.h"


#define MAX_LINE_LENGTH 256 * 1024

char line[MAX_LINE_LENGTH];

static const int TIMEOUT = 120000;
static const int WAIT_INTERVAL = 10;
static char *connectionFile;


void processRequest(char *request, char *responseFile) {

	// connect to server and send request (along with user ID)
	struct stat buf;
	if (stat(connectionFile, &buf) != 0) {
		fprintf(stderr, "Could not connect to index server (%s).\n", connectionFile);
		exit(1);
	}

	FILE *f = fopen(connectionFile, "w");
	if (f == NULL) {
		fprintf(stderr, "Could not connect to index server (%s).\n", connectionFile);
		exit(1);
	}
	fprintf(f, "%i %s %s\n", geteuid(), responseFile, request);
	fclose(f);

	// wait for response from server
	f = fopen(responseFile, "r");
	assert(f != NULL);
	int timeOutLeft = TIMEOUT;
	bool newLine = true;
	bool EOFseen = false;
	while (timeOutLeft > 0) {
		char line[1024];
		if (fgets(line, 1020, f) != NULL) {
			if (line[0] == '@') {
				if (newLine)
					EOFseen = true;
			}
			if (line[0] != 0) {
				if (line[strlen(line) - 1] == '\n')
					newLine = true;
				else
					newLine = false;
				printf("%s", line);
			}
		}
		else if (EOFseen)
			break;
		else {
			waitMilliSeconds(WAIT_INTERVAL);
			timeOutLeft -= WAIT_INTERVAL;
		}
	}
	fclose(f);
	fflush(stdout);
	unlink(responseFile);
} // end of processRequest(char*, char*)


int main(int argc, char **argv) {
	initializeConfiguratorFromCommandLineParameters(argc, argv);
	connectionFile = (char*)malloc(MAX_CONFIG_VALUE_LENGTH);
	if (!getConfigurationValue("CONNECTION_FILE", connectionFile)) {
		char dir[65536];
		free(connectionFile);
		if (!getConfigurationValue("DIRECTORY", dir))
			connectionFile = duplicateString(DEFAULT_CONNECTION_FILE);
		else
			connectionFile = evaluateRelativePathName(dir, "authconn");
	}
	while (fgets(line, MAX_LINE_LENGTH - 2, stdin) != NULL) {
		char resultFile[256];
		int len = strlen(line);
		if (line[len - 1] == '\n')
			line[--len] = 0;
		sprintf(resultFile, "/tmp/searchresult-XXXXXXXX");
		randomTempFileName(resultFile);
		mkfifo(resultFile, DEFAULT_FILE_PERMISSIONS);
		processRequest(line, resultFile);
	}
	free(connectionFile);
	return 0;
} // end of main()


