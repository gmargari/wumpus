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
 * created: 2004-10-11
 * changed: 2005-05-15
 **/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "authconn_daemon.h"
#include "query_executor.h"
#include "../misc/all.h"
#include "../misc/logging.h"
#include "../query/query.h"


static const char * LOG_ID = static_cast<const char*>("AuthConnDaemon");


AuthConnDaemon::AuthConnDaemon(Index *index) {
	this->index = index;
	fifoName = evaluateRelativePathName(index->directory, "authconn");
	init();
} // end of AuthConnDaemon()


AuthConnDaemon::AuthConnDaemon(Index *index, char *connectionFIFO) {
	this->index = index;
	fifoName = (char*)malloc(strlen(connectionFIFO) + 2);
	strcpy(fifoName, connectionFIFO);
	init();
} // end of AuthConnDaemon(char*)


void AuthConnDaemon::init() {
	unlink(fifoName);
	fifo = mkfifo(fifoName, S_IWUSR | S_IRUSR);
	if (fifo == 0) {
		chmod(fifoName, S_IWUSR | S_IRUSR);
		status = STATUS_CREATED;
	}
	else {
		log(LOG_ERROR, LOG_ID, "Unable to create FIFO for authenticated communication.");
		status = STATUS_TERMINATED;
	}
} // end of init()


AuthConnDaemon::~AuthConnDaemon() {
	if (status != STATUS_RUNNING) {
		while ((status != STATUS_RUNNING) && (status != STATUS_TERMINATED))
			waitMilliSeconds(10);
		waitMilliSeconds(10);
	}

	if (!stopped()) {
		stop();
		while (!stopped()) {
			int fd = open(fifoName, O_WRONLY | O_NONBLOCK);
			if (fd >= 0) {
				char line[16];
				strcpy(line, "STOP\n");
				forced_write(fd, line, sizeof(line));
				close(fd);
				waitMilliSeconds(10);
			}
		}
	} // end if (!stopped())

	unlink(fifoName);
	free(fifoName);
	fifoName = NULL;
} // end of ~AuthConnDaemon()


static void blockSIGPIPE() {
	sigset_t newSet, oldSet;
	sigemptyset(&newSet);
	sigaddset(&newSet, SIGPIPE);
	sigprocmask(SIG_BLOCK, &newSet, &oldSet);
} // end of blockSIGPIPE()


void AuthConnDaemon::run() {
	if (fifo == 0) {
		blockSIGPIPE();

		while (!stopRequested()) {
			FILE *f = fopen(fifoName, "r");

			if (f != NULL) {
				char line[65536];

				while (true) {
					char *result = fgets(line, 65534, f);
					if ((stopRequested()) || (result == NULL))
						break;

					// do not accept very short queries, as they are syntactically
					// incorrect anyway
					int len = strlen(line);
					if (len < 3)
						continue;
					if (line[len - 1] == '\n')
						line[--len] = 0;

					// extract the UID given by the sender of the query (will be verified
					// later on)
					int pos = 0;
					char *uid = &line[pos];
					while ((line[pos] != 0) && (line[pos] != ' '))
						pos++;
					if (line[pos] == 0)
						continue;
					line[pos++] = 0;

					// extract the target file (usually a FIFO)
					char *targetFile = &line[pos];
					while ((line[pos] != 0) && (line[pos] != ' '))
						pos++;
					if (line[pos] == 0)
						continue;
					line[pos++] = 0;

					// extract query string and execute query
					char *queryString = &line[pos];
					executeQuery(uid, targetFile, queryString);
				} // end while (true)

				fclose(f);
			} // end if (f != NULL)

		} // end while (!isStopRequested())

	} // end if (fifo != 0)

	if (getLock()) {
		status = STATUS_TERMINATED;
		releaseLock();
	}
	else
		status = STATUS_TERMINATED;
} // end of run()


void AuthConnDaemon::executeQuery(char *uid, char *targetFile, char *queryString) {
	// first, check if the file given by "targetFile" belongs to user "uid";
	// if not, do not execute the query
	struct stat buf;
	if (stat(targetFile, &buf) != 0)
		return;
	int clientUID;
	if (sscanf(uid, "%d", &clientUID) != 1)
		return;
	if ((buf.st_uid != clientUID) && (strcmp(targetFile, "/dev/null") != 0))
		return;

	// create new thread to process query and return
	int outputFD = open(targetFile, O_WRONLY | O_TRUNC);
	if (outputFD < 0)
		return;
	Query *query = new Query(index, queryString, clientUID);
	QueryExecutor::executeQuery(query, outputFD);
} // end of executeQuery(char*, char*, char*)


