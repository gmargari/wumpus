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
 * Implementation of the ClientConnection class. Client connection management
 * is a little complicated. This is because we want to be able to kill connections
 * immediately when somebody requested the shutdown of an Index. Therefore, we
 * have to spawn a new process for every incoming query. When somebody wants to
 * shutdown the Index or when the client closes the connection, we simply kill
 * the process that is working on the query.
 *
 * author: Stefan Buettcher
 * created: 2004-11-26
 * changed: 2009-02-01
 **/


#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/wait.h>
#include <unistd.h>
#include "client_connection.h"
#include "conn_daemon.h"
#include "../index/fakeindex.h"
#include "../misc/all.h"
#include "../query/query.h"


static const char *LOG_ID = "ClientConnection";


ClientConnection::ClientConnection() {
}


ClientConnection::ClientConnection(Index *index, int fd, uid_t userID) {
	initialize(index, fd, userID);
}


void ClientConnection::initialize(Index *index, int fd, uid_t userID) {
	this->index = index;
	this->fd = fd;
	this->userID = userID;
	getConfigurationBool("FORK_ON_QUERY", &forkOnQuery, false);
} // end of initialize(Index*, int, uid_t)


ClientConnection::~ClientConnection() {
	if (status == STATUS_CREATED)
		status = STATUS_TERMINATED;
} // end of ~ClientConnection()


static void blockSIGPIPE() {
	sigset_t newSet, oldSet;
	sigemptyset(&newSet);
	sigaddset(&newSet, SIGPIPE);
	sigprocmask(SIG_BLOCK, &newSet, &oldSet);
} // end of blockSIGPIPE()


bool ClientConnection::authenticate(char *userNamePassword) {
	char *unpw = chop(userNamePassword);
	StringTokenizer *tok = new StringTokenizer(unpw, " \t\n");
	char *userName = tok->getNext();
	char *password = tok->getNext();
	bool result = false;
	if ((userName != NULL) && (password != NULL)) {
		char passwordFile[1024];
		if (getConfigurationValue("PASSWORD_FILE", passwordFile)) {
			FILE *f = fopen(passwordFile, "r");
			if (f != NULL) {
				char line[1024];
				while (fgets(line, 1022, f) != NULL) {
					if ((line[0] == '#') || (line[0] <= ' '))
						continue;
					if (line[strlen(line) - 1] == '\n')
						line[strlen(line) - 1] = 0;
					StringTokenizer *tok2 = new StringTokenizer(line, ":");
					char *fileUID = tok2->getNext();
					char *fileUserName = tok2->getNext();
					char *filePassword = tok2->getNext();
					if (filePassword != NULL)
						if ((strcmp(userName, fileUserName) == 0) && (strcmp(password, filePassword) == 0)) {
							int value = atoi(fileUID);
							if (value >= 0) {
								userID = (uid_t)value;
								result = true;
							}
						}
					delete tok2;
				}
				fclose(f);
			}
		}
	}
	if (!result) {
		uid_t userIdFromShadowFile = SecurityManager::authenticate(userName, password);
		if (userIdFromShadowFile != (uid_t)-1) {
			result = true;
			userID = userIdFromShadowFile;
		}
	}
	delete tok;
	free(unpw);
	if (!result)
		userID = Index::NOBODY;
	return result;
} // end of authenticate(char*)


void ClientConnection::waitForDataOrHUP() {
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN | POLLHUP | POLLRDNORM;
	pfd.revents = 0;
	while (pfd.revents == 0) {
		int n = poll(&pfd, 1, 200);
		if (n == 0)
			pfd.revents = 0;
	}
} // end of waitForDataOrHUP()


void ClientConnection::run() {
	char line[Query::MAX_RESPONSELINE_LENGTH];
	bufferSize = 0;
	buffer[0] = 0;
	blockSIGPIPE();

	// disable Nagle's algorithm in order to decrease communication latency
	// (i.e., send data to client as soon as they are in the queue)
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

	while (true) {
		// wait until data are available or the socket has been closed
		if (strchr(buffer, '\n') == NULL)
			waitForDataOrHUP();

		// check whether we have seen EOL; if not, read until EOL has been reached
		// or the buffer size exceeds a certain threshold; in the latter case, we simply
		// pretend having seen an EOL
		if (strchr(buffer, '\n') == NULL) {
			if (bufferSize >= sizeof(buffer) / 4) {
				buffer[bufferSize++] = '\n';
				buffer[bufferSize] = 0;
			}
			else {
				int len = read(fd, &buffer[bufferSize], sizeof(buffer) - 2 - bufferSize);
				if (len <= 0)
					break;
				bufferSize += len;
				buffer[bufferSize] = 0;
			}
		}

		// copy data in "buffer" to "line" (everything before the first '\n')
		buffer[bufferSize] = 0;
		char *newLine = strchr(buffer, '\n');
		if (newLine == NULL)
			continue;
		*newLine = 0;
		newLine++;
		strcpy(line, buffer);

		// move the remaining data in the buffer to the front
		assert((long)newLine > (long)buffer);
		bufferSize -= ((long)newLine) - ((long)buffer);
		memmove(buffer, newLine, bufferSize);

		// remove trailing whitespace characters
		int len = strlen(line);
		if (len > 0)
			while ((line[len - 1] > 0) && (line[len - 1] <= ' ')) {
				line[--len] = 0;
				if (len == 0)
					break;
			}

		// number of bytes written in response to the current line;
		// -1 is used to indicate that the socket has been closed
		int written = -1;

		if (len == 0) {
			sprintf(line, "@%d-%s\n", 1, "Empty line.");
			written = sendMessage(line);
		}
		else if (len > Query::MAX_QUERY_LENGTH) {
			sprintf(line, "@%d-%s\n", 1, "Query too long.");
			written = sendMessage(line);
		}
		else if (strncasecmp(line, "@login ", 7) == 0) {
			bool result = authenticate(&line[7]);
			if (result)
				sprintf(line, "@%d-%s\n", 0, "Authenticated.");
			else
				sprintf(line, "@%d-%s\n", 1, "Authentication failed.");
			written = sendMessage(line);
		}
		else if (strcasecmp(line, "@whoami") == 0) {
			sprintf(line, "%d\n@%d-%s\n", userID, 0, "Ok.");
			written = sendMessage(line);
		}
		else if (strcasecmp(line, "@nofork") == 0) {
			if ((userID == Index::SUPERUSER) || (userID == geteuid())) {
				forkOnQuery = false;
				sprintf(line, "@%d-%s\n", 0, "Fork-on-query disabled.");
				written = sendMessage(line);
			}
			else
				written = 0;
		}
		else if ((strcasecmp(line, "@quit") == 0) || (strcasecmp(line, "@exit") == 0)) {
			// close connection
			written = -1;
		}
		else {
			// process the command found in the line just read
			written = processLine(line);
		}

		// if nothing could be written to the socket: stop execution
		if (written < 0)
			break;

	} // end while (true)

	// send all pending data to the client and close socket
	close(fd);

	bool mustReleaseLock = getLock();
	status = STATUS_TERMINATED;
	if (mustReleaseLock)
		releaseLock();
} // end of run()


int ClientConnection::processLine(char *line) {
	snprintf(errorMessage, sizeof(errorMessage), "Line received: %s", line);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	char response[Query::MAX_RESPONSELINE_LENGTH];
	char message[Query::MAX_RESPONSELINE_LENGTH + 2];
	Query *q = new Query(index, line, userID);
	int queryType = q->getType();
	int result = -1;

	bool mustFork = forkOnQuery;
	if (queryType == q->QUERY_TYPE_UPDATE)
		mustFork = false;
	if (queryType == q->QUERY_TYPE_MISC)
		mustFork = false;

	if ((!mustFork) && (strncasecmp(line, "@getfile ", strlen("@getfile ")) != 0)) {
		// processing an @update or an @misc query can affect the content of the
		// index; therefore, we have to process the query inside this thread
		q->parse();
		while (q->getNextLine(response)) {
			sprintf(message, "%s\n", response);
			result = sendMessage(message);
		}
		int statusCode;
		q->getStatus(&statusCode, response);
		sprintf(message, "@%d-%s\n", statusCode, response);
		result = sendMessage(message);
	}
	else {
		// special handling for @getfile queries
		if (strncasecmp(line, "@getfile ", strlen("@getfile ")) == 0) {
			delete q;
			return processGetFileQuery(line);
		}

		// pre-parse the query, using a new Query and a FakeIndex object; if
		// parsing is successful, this guarantees that all necessary data have been
		// loaded into the cache
		FakeIndex *fakeIndex = new FakeIndex(index);
		Query *fakeQuery = new Query(fakeIndex, line, userID);
		char syntaxOk = fakeQuery->parse();
		if (!syntaxOk) {
			int statusCode;
			fakeQuery->getStatus(&statusCode, response);
			sprintf(message, "@%d-%s\n", statusCode, response);
		}		
		delete fakeQuery;
		delete fakeIndex;
		if (!syntaxOk) {
			delete q;
			return sendMessage(message);
		}

		// If the query cannot change the index contents, we are allowed to create
		// a new process that will work on this query. Creating a new process is
		// important because it allows us to kill the process as soon as the client
		// closes the connection. If the query is processed by a thread instead of
		// a process, this is more difficult, since we have to release all resources
		// allocated by that thread before we can actually terminate it.
		pid_t childProcess = fork();

		if (childProcess == (pid_t)-1) {
			// cannot fork or cannot create pipe
			sprintf(message, "@%d-%s\n", 1, "Unable to create new process.");
			sendMessage(message);
			result = -1;
		}
		else if (childProcess == 0) {
			// we are the child: give us better priority value and process query
			setpriority(PRIO_PROCESS, getpid(), 0);
			Lockable::disableLocking();
			GlobalVariables::forkCount++;
			q->parse();
			while (q->getNextLine(response)) {
				sprintf(message, "%s\n", response);
				sendMessage(message);
			}
			int statusCode;
			q->getStatus(&statusCode, response);
			sprintf(message, "@%d-%s\n", statusCode, response);
			if (sendMessage(message) > 0) {
				close(fd);
				exit(0);
			}
			exit(1);
		}
		else {
			// we are the parent: monitor both connection and child
			int timeElapsed = 0;
			int waitInterval = 5;
			while (true) {
				waitMilliSeconds(waitInterval);
				timeElapsed += waitInterval;
				if ((timeElapsed > waitInterval * 20) && (waitInterval < 20))
					waitInterval *= 2;
				int status;
				if (waitpid(childProcess, &status, WNOHANG) > 0) {
					// if the child has finished execution, leave loop and wait for further
					// queries from the client
					status = WEXITSTATUS(status);
					if (status == 0)
						result = 1;
					else
						result = -1;
					break;
				}
				char buf[256];
				int recvResult = recv(fd, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);
				bool connectionClosed = false;
				if (recvResult == 0) {
					// data available and zero bytes to read: this smells like connection closed
					connectionClosed = true;
				}
				else if (recvResult > 0) {
					if (recvResult + bufferSize >= sizeof(buffer) - 2) {
						int len = read(fd, buf, recvResult);
						if (len <= 0)
							connectionClosed = true;
					}
					else {
						int len = read(fd, &buffer[bufferSize], sizeof(buffer) - 2 - bufferSize);
						if (len <= 0)
							connectionClosed = true;
						bufferSize += len;
					}
				}
				if (connectionClosed) {
					sprintf(errorMessage, "Killing child process after connection closed by client.");
					log(LOG_DEBUG, LOG_ID, errorMessage);
					kill(childProcess, SIGKILL);
					waitpid(childProcess, &status, 0);
					result = -1;
					break;
				}
			}
		}
	} // end else [not an @update or @misc query]

	delete q;
	return result;
} // end of processLine(char*)


int ClientConnection::processGetFileQuery(char *line) {
	char message[512];

	char *ptr = &line[strlen("@getfile")];
	while ((*ptr > 0) && (*ptr <= ' '))
		ptr++;
	if (ptr[0] == 0)
		return sendMessage("@1-Syntax error.\n");
	if (ptr[strlen(ptr) - 1] == '\n')
		ptr[strlen(ptr) - 1] = 0;

	if (!index->mayAccessFile(userID, ptr)) {
		sprintf(message, "@%d-%s\n", ERROR_ACCESS_DENIED, ERROR_MESSAGES[ERROR_ACCESS_DENIED]);
		return sendMessage(message);
	}

	struct stat buf;
	int fd = open(ptr, O_RDONLY);
	if (fd < 0) {
		sprintf(message, "@%d-%s\n", ERROR_ACCESS_DENIED, ERROR_MESSAGES[ERROR_ACCESS_DENIED]);
		return sendMessage(message);
	}
	if (fstat(fd, &buf) != 0) {
		close(fd);
		sprintf(message, "@%d-%s\n", ERROR_ACCESS_DENIED, ERROR_MESSAGES[ERROR_ACCESS_DENIED]);
		return sendMessage(message);
	}
	if (buf.st_size > MAX_GETFILE_FILE_SIZE) {
		close(fd);
		sprintf(message, "@%d-%s\n", ERROR_FILE_TOO_LARGE, ERROR_MESSAGES[ERROR_FILE_TOO_LARGE]);
		return sendMessage(message);
	}

	char buffer[1024];
	int size = buf.st_size, total = 0, written, result;

	char *mimeType = getFileType(ptr, true);
	if (mimeType == NULL)
		sendMessage("application/unknown\n");
	else {
		sendMessage(mimeType);
		free(mimeType);
	}
	char *fileType = getFileType(ptr, false);
	if (fileType == NULL)
		sendMessage("NULL\n");
	else {
		sendMessage(fileType);
		free(fileType);
	}
	sprintf(message, "%d\n", size);
	sendMessage(message);

	while ((result = forced_read(fd, buffer, sizeof(buffer))) > 0) {
		int toWrite = result, written = 0;
		while (toWrite > 0) {
			result = write(this->fd, &buffer[written], toWrite);
			if (result < 0)
				if ((errno != EINTR) && (errno != -EINTR))
					break;
			toWrite -= result;
			written += result;
		}
		assert(toWrite >= 0);
		if (result > 0)
			total += written;
		else
			break;
	}
	close(fd);

	memset(buffer, 0, sizeof(buffer));
	while (total < size) {
		if (total + sizeof(buffer) <= size)
			written = forced_write(this->fd, buffer, sizeof(buffer));
		else
			written = forced_write(this->fd, buffer, size - total);
		if (written > 0)
			total += written;
		else
			break;
	}

	sprintf(message, "@%d-%s\n", 0, "Ok.");
	return sendMessage(message);
} // end of processGetFileQuery(char*)


int ClientConnection::sendMessage(const char *message) {
	if (fd < 0)
		return -1;
	else
		return forced_write(fd, message, strlen(message));
} // end of sendMessage(char*)


void ClientConnection::closeSocket() {
	shutdown(fd, SHUT_RDWR);
	close(fd);
} // end of closeSocket()


