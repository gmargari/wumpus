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
 * See filesys_daemon.h for documentation.
 *
 * author: Stefan Buettcher
 * created: 2004-10-07
 * changed: 2009-02-01
 **/


#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "filesys_daemon.h"
#include "query_executor.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"
#include "../query/query.h"


const char * FileSysDaemon::LOG_ID = "FileSysDaemon";


FileSysDaemon::FileSysDaemon(Index *index, char *baseDirectory, time_t lastScan) {
	owner = index;
	procFile = -1;
	this->lastScan = lastScan;

	baseDir = duplicateString(baseDirectory);
	if (baseDir == NULL)
		baseDir = duplicateString("/");
	else if (baseDir[0] != 0)
		if (baseDir[strlen(baseDir) - 1] != '/')
			baseDir = concatenateStringsAndFree(baseDir, duplicateString("/"));

	if (!getConfigurationValue("FSCHANGE_FILE", fschangeFile))
		fschangeFile[0] = 0;

	eventQueue = new EventQueue(index);
	eventQueue->start();
	status = STATUS_CREATED;

} // end of FileSysDaemon(Index*)


FileSysDaemon::~FileSysDaemon() {
	if (!stopped()) {
		stop();
		while (!stopped()) { }
		while (isScanning)
			waitMilliSeconds(WAIT_INTERVAL);
		if (eventQueue != NULL) {
			delete eventQueue;
			eventQueue = NULL;
		}
	}
	if (baseDir != NULL) {
		free(baseDir);
		baseDir = NULL;
	}
} // end of ~FileSysDaemon()


void FileSysDaemon::stop() {
	log(LOG_DEBUG, LOG_ID, "Stopping FileSysDaemon.");
	bool mustReleaseLock = getLock();
	// set termination flag
	if (status != STATUS_TERMINATED)
		status = STATUS_TERMINATING;
	// close file handle to proc file
	if (procFile >= 0) {
		int fd = procFile;
		procFile = -1;
		close(fd);
	}
	if (mustReleaseLock)
		releaseLock();
	while (!stopped()) {
		// by writing something to a file, we guarantee that the FileSysDaemon
		// returns from one of its blocking calls
		waitMilliSeconds(20);
	}
	log(LOG_DEBUG, LOG_ID, "FileSysDaemon stopped.");
} // end of stop()


static void * startDiskScan(void *data) {
	if (data == NULL)
		return NULL;
	FileSysDaemon *fsd = (FileSysDaemon*)data;
	fsd->scanFileSystem();
	return NULL;
} // end of startDiskScan(void*)


static int readFromEventFile(void *buffer) {
	char fschangeFile[MAX_CONFIG_VALUE_LENGTH];
	char *eventBuffer = (char*)buffer;
	int bufferSize = strlen(eventBuffer);
	if (getConfigurationValue("FSCHANGE_FILE", fschangeFile)) {
		int fd = open(fschangeFile, O_RDONLY);
		if (fd >= 0) {
			int gelesen = read(fd, &eventBuffer[bufferSize], 4096);
			if (gelesen > 0)
				eventBuffer[bufferSize + gelesen] = 0;
			close(fd);
		}
	}
	return 0;
} // end of readFromEventFile(void*)


void FileSysDaemon::run() {
	if (stopRequested())
		return;

	int minutesBetweenScans;
	getConfigurationInt("TIME_BETWEEN_FS_SCANS", &minutesBetweenScans, 1440);
	checkMountPoints();

	char fsEventBuffer[65536];
	int bufferPos = 0, bufferSize = 0;
	fsEventBuffer[0] = 0;
	char childStack[MAX_CONFIG_VALUE_LENGTH * 4];

	// loop until somebody asks us to shutdown
	while (!stopRequested()) {
		// slow down a bit such that we don't eat up *all* system resources
		waitMilliSeconds(EVENT_WAIT_INTERVAL);
		if (stopRequested())
			break;

		do {
			if (stopRequested())
				break;
			char *eol = strchr(&fsEventBuffer[bufferPos], '\n');
			if (eol != NULL) {
				*eol = 0;
				time_t now = time(NULL);
				int len = strlen(&fsEventBuffer[bufferPos]);
				if (len > 4) {
					notifyIndex(&fsEventBuffer[bufferPos], now);
					bufferPos += (len + 1);
				}
				continue;
			}
			if (bufferPos > sizeof(fsEventBuffer) / 4) {
				bufferSize -= bufferPos;
				memcpy(fsEventBuffer, &fsEventBuffer[bufferPos], bufferSize);
				bufferPos = 0;
			}
			int fd = open(fschangeFile, O_RDONLY | O_NONBLOCK);
			if (fd >= 0) {
				int gelesen = read(fd, &fsEventBuffer[bufferSize], sizeof(fsEventBuffer) - bufferSize - 4);
				if (gelesen > 0) {
					bufferSize += gelesen;
					fsEventBuffer[bufferSize] = 0;
				}
				close(fd);
			}
		} while (strchr(&fsEventBuffer[bufferPos], '\n') != NULL);

		// check whether it is time to do another pass over the file system
		if (minutesBetweenScans > 0) {
			time_t now = time(NULL);
			if ((lastScan + minutesBetweenScans * 60 < now) && (!isScanning)) {
				isScanning = true;
				pthread_t thread;
				pthread_create(&thread, NULL, startDiskScan, this);
				pthread_detach(thread);
			}
		}

	} // end while (!stopRequested())
	
	if (getLock()) {
		status = STATUS_TERMINATED;
		releaseLock();
	}
	else
		status = STATUS_TERMINATED;
} // end of run()


void FileSysDaemon::notifyIndex(char *event, time_t timeStamp) {
	bool mustReleaseLock = getLock();
	if (eventQueue == NULL) {
		char *queryString = concatenateStrings("@update ", event);
		Query *q = new Query(owner, queryString, Index::SUPERUSER);
		free(queryString);
		QueryExecutor::executeQuery(q, -1);
	}
	else {
		int queueLength = eventQueue->getQueueLength();
		while ((!stopped()) && (!stopRequested()) && (queueLength >= EventQueue::MAX_QUEUE_SIZE - 1)) {
			if (mustReleaseLock)
				releaseLock();
			waitMilliSeconds(50);
			mustReleaseLock = getLock();
			if (eventQueue == NULL)
				break;
			queueLength = eventQueue->getQueueLength();
		}
		if ((!stopped()) && (!stopRequested()))
			eventQueue->enqueue(event, timeStamp);
	}
	if (mustReleaseLock)
		releaseLock();
} // end of notifyIndex(char*, time_t)


void FileSysDaemon::checkMountPoints() {
	FILE *f = fopen("/etc/mtab", "r");
	if (f == NULL)
		return;
	char line[8192];
	while (fgets(line, 8190, f) != NULL) {
		StringTokenizer *tok = new StringTokenizer(line, "\t ");
		char *deviceFile = tok->getNext();
		char *mountPoint = tok->getNext();
		char *type = tok->getNext();
		char *options = tok->getNext();
		if ((deviceFile == NULL) || (mountPoint == NULL) || (type == NULL) || (options == NULL)) {
			delete tok;
			continue;
		}
		if (mountPoint[0] == '/') {
			char *eventString = (char*)malloc(strlen(deviceFile) + strlen(mountPoint) + 32);
			sprintf(eventString, "%s\t%s\t%s", "MOUNT", deviceFile, mountPoint);
			notifyIndex(eventString, time(NULL) - EventQueue::HOT_POTATO_INTERVAL - 1);
			free(eventString);
		}
		delete tok;
	}
	fclose(f);
} // end of checkMountPoints()


void FileSysDaemon::scanFileSystem() {
	if (baseDir == NULL)
		scanDirectory(const_cast<char*>("/"), true);
	else
		scanDirectory(baseDir, true);
	log(LOG_DEBUG, LOG_ID, "File system scan finished.");
	lastScan = time(NULL);
	isScanning = false;
} // end of scanFileSystem()


static char * addToQueue(int &queuePos, int &queueSize, int &bufferPos,
			int &bufferSize, int &bufferAllocated, char *queueBuffer, char *newElem) {
	int len = strlen(newElem) + 1;
	if (bufferSize + len + sizeof(int32_t) >= bufferAllocated) {
		bufferAllocated = (int)(bufferAllocated * 1.37);
		queueBuffer = (char*)realloc(queueBuffer, bufferAllocated);
	}
	int32_t bp = bufferPos;
	memcpy(&queueBuffer[bufferSize], &bp, sizeof(int32_t));
	strcpy(&queueBuffer[bufferSize + sizeof(int32_t)], newElem);
	bufferSize += len + sizeof(int32_t);
	queueSize++;
	return queueBuffer;
} // end of addToQueue(...)


static char * reconstructPath(char *queueBuffer, int bufferPos) {
	char temp[2048];
	int tempPos = 2048;
	int originalBufferPos = bufferPos;
	while (true) {
		int32_t predecessor = 0;
		memcpy(&predecessor, &queueBuffer[bufferPos], sizeof(int32_t));
		char *elem = &queueBuffer[bufferPos + sizeof(int32_t)];
		int len = strlen(elem);
		strcpy(&temp[tempPos - len - 1], elem);
		if (tempPos < 2048)
			temp[tempPos - 1] = '/';
		tempPos = tempPos - len - 1;
		if (predecessor == bufferPos)
			break;
		bufferPos = predecessor;
	}
	if (temp[tempPos] != '/')
		temp[--tempPos] = '/';
	return duplicateString(&temp[tempPos]);
} // end of reconstructPath(char*, int)


int FileSysDaemon::scanDirectory(char *baseDir, bool recursive) {
	int queuePos = 0;
	int queueSize = 0;
	int bufferPos = 0;
	int bufferSize = 0;
	int bufferAllocated = 65536;
	char *queueBuffer = (char*)malloc(bufferAllocated);
	baseDir = duplicateString(baseDir);
	if (baseDir[strlen(baseDir) - 1] == '/')
		baseDir[strlen(baseDir) - 1] = 0;

	queueBuffer = addToQueue(queuePos, queueSize, bufferPos, bufferSize,
			bufferAllocated, queueBuffer, baseDir);

	while ((queuePos < queueSize) && (!stopRequested())) {
		char *path = reconstructPath(queueBuffer, bufferPos);
		struct stat buf;
		if ((Index::directoryAllowed(path)) && (lstat(path, &buf) == 0)) {
			if ((S_ISDIR(buf.st_mode)) && (strlen(path) < 1024)) {
				DIR *dir = opendir(path);
				if (dir != NULL) {
					struct dirent *file;
					while (((file = readdir(dir)) != NULL) && (!stopRequested())) {
						if (strcmp(file->d_name, ".") == 0)
							continue;
						if (strcmp(file->d_name, "..") == 0)
							continue;
						if (strlen(file->d_name) < 80) {
							char *fullPath = evaluateRelativePathName(path, file->d_name);
							if ((strlen(fullPath) < 1024) && (lstat(fullPath, &buf) == 0)) {
								if (S_ISDIR(buf.st_mode)) {
									queueBuffer = addToQueue(queuePos, queueSize, bufferPos,
											bufferSize, bufferAllocated, queueBuffer, file->d_name);
								}
								else if (S_ISREG(buf.st_mode)) {
									// if it is a file, try to index it
									char event[2048];
									sprintf(event, "CREATE\t%s", fullPath);
									notifyIndex(event, time(NULL) - EventQueue::HOT_POTATO_INTERVAL - 1);
								}
							}
							free(fullPath);
						}
					} // end while ((file = readdir(dir)) != NULL)
					closedir(dir);
				}
			}
		} // end if ((Index::directoryAllowed(path)) && (lstat(path, &buf) == 0))
		queuePos++;
		if (queuePos < queueSize) {
			bufferPos += sizeof(int32_t);
			while (queueBuffer[bufferPos] != 0)
				bufferPos++;
			bufferPos++;
		}
		free(path);
	} // end while (queuePos < queueSize)

	lastScan = time(NULL);
	free(baseDir);
	free(queueBuffer);
	return queuePos;
} // end of scanDirectory(char*, bool)


