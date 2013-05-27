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
 * Implementation of the CompressedInputStream class.
 *
 * author: Stefan Buettcher
 * created: 2005-07-22
 * changed: 2005-07-30
 **/


#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "compressed_inputstream.h"
#include "html_inputstream.h"
#include "xml_inputstream.h"
#include "trec_inputstream.h"
#include "text_inputstream.h"
#include "../misc/all.h"
#include "../misc/logging.h"


static const char * LOG_ID = "CompressedInputStream";


CompressedInputStream::CompressedInputStream() {
	childProcess = (pid_t)0;
	uncompressedStream = NULL;
	fileName = NULL;
	decompressionCommand = NULL;
	inputFile = -1;
} // end of CompressedInputStream()


CompressedInputStream::~CompressedInputStream() {
	if (childProcess > 0) {
		int status;
		kill(childProcess, SIGKILL);
		waitpid(childProcess, &status, 0);
	}
	if (uncompressedStream != NULL) {
		delete uncompressedStream;
		uncompressedStream = NULL;
	}
	if (decompressionCommand != NULL) {
		free(decompressionCommand);
		decompressionCommand = NULL;
	}
	if (fileName != NULL) {
		free(fileName);
		fileName = NULL;
	}
} // end of ~CompressedInputStream()


int CompressedInputStream::getDocumentType() {
	if (uncompressedStream == NULL)
		return DOCUMENT_TYPE_UNKNOWN;
	else
		return uncompressedStream->getDocumentType();
} // end of getDocumentType()


bool CompressedInputStream::getNextToken(InputToken *result) {
	if (uncompressedStream == NULL)
		return false;
	else if (uncompressedStream->getNextToken(result)) {
		result->canBeUsedAsLandmark = false;
		return true;
	}
	else
		return false;
} // end of getNextToken(InputToken*)


void CompressedInputStream::getPreviousChars(char *buffer, int bufferSize) {
	if (uncompressedStream == NULL) {
		for (int i = 0; i < bufferSize; i++)
			buffer[i] = 0;
	}
	else
		uncompressedStream->getPreviousChars(buffer, bufferSize);
} // end of getPreviousChars(char*, int)


void CompressedInputStream::initialize() {
	int status, documentType = -1;
	byte buffer[4096];
	inputFile = -1;
	if ((decompressionCommand == NULL) || (fileName == NULL))
		return;
	struct stat buf;
	if (stat(fileName, &buf) == 0)
		if (S_ISREG(buf.st_mode))
			inputFile = open(fileName, O_RDONLY);
	if (inputFile < 0)
		return;

	// fork once in order to find out the document type of the data inside
	// the compressed file
	int pipeFDs[2];
	if (pipe(pipeFDs) != 0) {
		close(inputFile);
		inputFile = -1;
		return;
	}
	childProcess = fork();
	switch (childProcess) {
		case -1:
			// fork failed
			close(pipeFDs[0]);
			close(pipeFDs[1]);
			close(inputFile);
			inputFile = -1;
			return;
		case 0:
			// child process: start decompression program
			close(pipeFDs[0]);
			dup2(inputFile, fileno(stdin));
			dup2(pipeFDs[1], fileno(stdout));
			execlp(decompressionCommand, decompressionCommand, "-c", "-d", NULL);
			exit(1);
		default:
			// parent process: read first N kilobytes and decide which input stream
			// to use to parse the data
			close(pipeFDs[1]);
			int bufferSize = forced_read(pipeFDs[0], buffer, sizeof(buffer) - 1);
			if (bufferSize < 0)
				bufferSize = 0;
			buffer[bufferSize] = 0;
			kill(childProcess, SIGKILL);
			waitpid(childProcess, &status, 0);
			close(pipeFDs[0]);
			if (TRECInputStream::canProcess(fileName, buffer, bufferSize))
				documentType = DOCUMENT_TYPE_TREC;
			else if (HTMLInputStream::canProcess(fileName, buffer, bufferSize))
				documentType = DOCUMENT_TYPE_HTML;
			else if (XMLInputStream::canProcess(fileName, buffer, bufferSize))
				documentType = DOCUMENT_TYPE_XML;
			else if (TextInputStream::canProcess(fileName, buffer, bufferSize))
				documentType = DOCUMENT_TYPE_TEXT;
			break;
	}

	// fork a second time, this time for real; create the underlying input
	// stream depending on the value of "documentType"
	if (documentType >= 0)
		if (pipe(pipeFDs) != 0)
			documentType = -1;
	if (documentType < 0) {
		close(inputFile);
		inputFile = -1;
		return;
	}
	lseek(inputFile, (off_t)0, SEEK_SET);
	childProcess = fork();
	switch (childProcess) {
		case -1:
			// fork failed
			close(pipeFDs[0]);
			close(pipeFDs[1]);
			close(inputFile);
			inputFile = -1;
			return;
		case 0:
			// child process: start decompression program
			close(pipeFDs[0]);
			dup2(inputFile, fileno(stdin));
			dup2(pipeFDs[1], fileno(stdout));
			execlp(decompressionCommand, decompressionCommand, "-c", "-d", NULL);
			exit(1);
		default:
			// parent process: start new InputStream, reading from the child
			// process's output stream
			close(pipeFDs[1]);
			close(inputFile);
			inputFile = -1;
			switch (documentType) {
				case DOCUMENT_TYPE_TREC:
					uncompressedStream = new TRECInputStream(pipeFDs[0]);
					break;
				case DOCUMENT_TYPE_HTML:
					uncompressedStream = new HTMLInputStream(pipeFDs[0]);
					break;
				case DOCUMENT_TYPE_XML:
					uncompressedStream = new XMLInputStream(pipeFDs[0]);
					break;
				case DOCUMENT_TYPE_TEXT:
					uncompressedStream = new TextInputStream(pipeFDs[0]);
					break;
				default:
					log(LOG_ERROR, LOG_ID, "Illegal document type.");
			}
			break;
	}

} // end of initialize()


