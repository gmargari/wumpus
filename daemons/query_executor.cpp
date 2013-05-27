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
 * Implementation of the QueryExecutor class.
 *
 * author: Stefan Buettcher
 * created: 2005-03-14
 * changed: 2009-02-01
 **/


#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "query_executor.h"
#include "../misc/all.h"


static void pushString(const char *string, int fd) {
	if (fd < 0)
		return;
	int len = strlen(string);
	int written = 0;
	int cnt = 0;
	while (written < len) {
		int result = write(fd, &string[written], len - written);
		if (result < 0) {
			if (errno != -EINTR)
				break;
			else if (++cnt > 3)
				break;
		}
		else
			written += result;
	}
} // end of pushString(char*, int)


static void *executionFunction(void *data) {
	char resultLine[Query::MAX_RESPONSELINE_LENGTH];
	QuerySessionDescriptor *qsd = (QuerySessionDescriptor*)data;
	if (qsd->query->parse()) {
		while (qsd->query->getNextLine(resultLine)) {
			pushString(resultLine, qsd->outputFD);
			pushString("\n", qsd->outputFD);
		}
	}
	int statusCode;
	char statusLine[1024];
	qsd->query->getStatus(&statusCode, statusLine);
	sprintf(resultLine, "@%d-%s\n", statusCode, statusLine);
	pushString(resultLine, qsd->outputFD);
	delete qsd->query;
	QueryExecutor::shutdownAndClose(qsd->outputFD);
	free(qsd);
} // end of executionFunction(void*)


void QueryExecutor::executeQuery(Query *query, int outputFD) {
	pthread_t threadID;
	QuerySessionDescriptor *qsd = typed_malloc(QuerySessionDescriptor, 1);
	qsd->query = query;
	qsd->outputFD = outputFD;
	pthread_create(&threadID, NULL, executionFunction, qsd);
	pthread_detach(threadID);
} // end of executeQuery(char*, int)


void QueryExecutor::shutdownAndClose(int fd) {
	if (fd < 0)
		return;
	if (stdin != NULL)
		if (fd == fileno(stdin))
			return;
	if (stdout != NULL)
		if (fd == fileno(stdout))
			return;
	if (stderr != NULL)
		if (fd == fileno(stderr))
			return;
	shutdown(fd, SHUT_RDWR);
	close(fd);
} // end of shutdownAndClose(int)



