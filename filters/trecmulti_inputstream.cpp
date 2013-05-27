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
 * Implementation of the TRECMultiInputStream class.
 *
 * author: Stefan Buettcher
 * created: 2005-07-21
 * changed: 2005-07-21
 **/


#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "trecmulti_inputstream.h"
#include "../misc/alloc.h"
#include "../misc/logging.h"


static const char *LOG_ID = "TRECMultiInputStream";


TRECMultiInputStream::TRECMultiInputStream(const char *fileName) {
	// open input file and parse it, creating an internal representation of
	// the individual input streams
	FILE *file = NULL;
	if (fileName == NULL) {
		inputFile = -1;
		initialize();
		return;
	}
	else if (fileName[0] == 0)
		file = stdin;
	else
		file = fopen(fileName, "r");
	if (fgets(line, sizeof(line), file) == NULL) line[0] = 0;
	assert(strcasecmp(line, "<TREC_MULTIPLE_INPUT_STREAM>\n") == 0);
	streamCount = 0;

	for (int i = 0; i < MAX_STREAM_COUNT; i++) {
		fileNames[i] = NULL;
		currentFile[i] = NULL;
	}

	// do a loop over all stream definitions in the file
	while (true) {		
		line[0] = 0;
		while ((line[0] == 0) || (line[0] == '\n'))
			if (fgets(line, sizeof(line), file) == NULL)
				goto error;
		if (strncasecmp(line, "</TREC_MULTIPLE_INPUT_STREAM>", 29) == 0)
			break;
		if (streamCount >= MAX_STREAM_COUNT)
			goto error;
		char *p = line;
		while ((*p > 0) && (*p <= ' '))
			p++;
		if (strncasecmp(p, "Stream:", 7) != 0)
			goto error;
		fileNames[streamCount] = new StringTokenizer(&p[7], " \t\n");
		char *token;
		while ((token = fileNames[streamCount]->getNext()) != NULL) {
			if (token[0] != 0) {
				currentFile[streamCount] = fopen(token, "r");
				break;
			}
		}
		nextDocument[streamCount][0] = 0;
		streamCount++;
	} // end while (true)
	fclose(file);
	file = NULL;

	// Create a pipe and fork. The child process will read data from the input
	// streams and write stuff to the pipe. The parent process reads from the pipe
	// and returns tokens to the indexing system.
	int pipeFDs[2];
	if (pipe(pipeFDs) != 0)
		goto error;
	childProcess = fork();
	switch (childProcess) {
		case -1:
			// unable to fork
			close(pipeFDs[0]);
			close(pipeFDs[1]);
			goto error;
		case 0:
			// child process
			close(pipeFDs[0]);
			processInputStreams(pipeFDs[1]);
			close(pipeFDs[1]);
			exit(0);
		default:
			// parent process
			close(pipeFDs[1]);
			inputFile = pipeFDs[0];
			initialize();
			return;
	} // end switch (childProcess)

error:
	log(LOG_ERROR, LOG_ID, "Illegal stream definition.");
	if (file != NULL)
		fclose(file);
	inputFile = -1;
	initialize();
} // end of TRECMultiInputStream(char*)


TRECMultiInputStream::~TRECMultiInputStream() {
	if (childProcess > (pid_t)0) {
		int status;
		kill(childProcess, SIGKILL);
		waitpid(childProcess, &status, 0);
	}
	for (int i = 0; i < streamCount; i++) {
		if (currentFile[i] != NULL)
			fclose(currentFile[i]);
		if (fileNames[i] != NULL)
			delete fileNames[i];
	}
	if (inputFile >= 0)
		close(inputFile);
} // end of ~TRECMultiInputStream()


bool TRECMultiInputStream::getNextLine(int whichStream, char *line, int maxLen) {
	if (currentFile[whichStream] == NULL) {
		char *token;
		while ((token = fileNames[whichStream]->getNext()) != NULL) {
			if (token[0] != 0) {
				currentFile[whichStream] = fopen(token, "r");
				return getNextLine(whichStream, line, maxLen);
			}
		}
		return false;
	}
	if (fgets(line, maxLen, currentFile[whichStream]) != NULL)
		return true;
	fclose(currentFile[whichStream]);
	currentFile[whichStream] = NULL;
	return getNextLine(whichStream, line, maxLen);
} // end of getNextLine(int, char*)


void TRECMultiInputStream::processInputStreams(int outFD) {
	currentDocument[0] = 0;

	for (int i = 0; i < streamCount; i++) {
		while ((nextDocument[i][0] == 0) && (currentFile[i] != NULL)) {
			if (!getNextLine(i, line, sizeof(line)))
				break;
			char *toReplace = strstr(line, "<docno>");
			if (toReplace == NULL)
				toReplace = strstr(line, "<DOCNO>");
			if (toReplace == NULL)
				forced_write(outFD, line, strlen(line));
			else {
				toReplace = &toReplace[7];
				while ((*toReplace > 0) && (*toReplace <= ' '))
					toReplace++;
				char *endTag = strstr(toReplace, "</docno>");
				if (endTag == NULL)
					endTag = strstr(toReplace, "</DOCNO>");
				if (endTag != NULL)
					*endTag = 0;
				strcpy(nextDocument[i], toReplace);
				break;
			}
		}
	}

	while (true) {
		int nextStream = -1;
		for (int i = 0; i < streamCount; i++)
			if (nextDocument[i][0] != 0) {
				if (nextStream < 0)
					nextStream = i;
				else {
					int delta = strlen(nextDocument[i]) - strlen(nextDocument[nextStream]);
					if (delta < 0)
						nextStream = i;
					else if ((delta == 0) && (strcmp(nextDocument[i], nextDocument[nextStream]) < 0))
						nextStream = i;
				}
			}
		if (nextStream < 0)
			break;
		if (strcmp(nextDocument[nextStream], currentDocument) != 0) {
			if (currentDocument[0] != 0) {
				sprintf(line, "</DOC>\n");
				forced_write(outFD, line, strlen(line));
			}
			strcpy(currentDocument, nextDocument[nextStream]);
			sprintf(line, "<DOC>\n<DOCNO>%s</DOCNO>\n", currentDocument);
			forced_write(outFD, line, strlen(line));
		}

		nextDocument[nextStream][0] = 0;

		while (getNextLine(nextStream, line, sizeof(line))) {
			char *toReplace;
			do {
				if ((toReplace = strstr(line, "<doc>")) != NULL)
					strcpy(toReplace, &toReplace[5]);
				else if ((toReplace = strstr(line, "</doc>")) != NULL)
					strcpy(toReplace, &toReplace[6]);
				else if ((toReplace = strstr(line, "<DOC>")) != NULL)
					strcpy(toReplace, &toReplace[5]);
				else if ((toReplace = strstr(line, "</DOC>")) != NULL)
					strcpy(toReplace, &toReplace[6]);
			} while (toReplace != NULL);
			toReplace = strstr(line, "<docno>");
			if (toReplace == NULL)
				toReplace = strstr(line, "<DOCNO>");
			if (toReplace == NULL)
				forced_write(outFD, line, strlen(line));
			else {
				toReplace = &toReplace[7];
				while ((*toReplace > 0) && (*toReplace <= ' '))
					toReplace++;
				char *endTag = strstr(toReplace, "</docno>");
				if (endTag == NULL)
					endTag = strstr(toReplace, "</DOCNO>");
				if (endTag != NULL)
					*endTag = 0;
				strcpy(nextDocument[nextStream], toReplace);
				break;
			}
		} // end while (getNextLine(nextStream, line))

	} // end while (true)

	if (currentDocument[0] != 0) {
		sprintf(line, "</DOC>\n");
		forced_write(outFD, line, strlen(line));
	}
} // end of processInputStreams(int)


int TRECMultiInputStream::getDocumentType() {
	return DOCUMENT_TYPE_TRECMULTI;
}


bool TRECMultiInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < 60)
		return false;
	if (strncasecmp((char*)fileStart, "<TREC_MULTIPLE_INPUT_STREAM>\n", 29) != 0)
		return false;
	if (strstr((char*)fileStart, "Stream: ") == NULL)
		return false;
	return true;
} // end of canProcess(char*, byte*, int)


