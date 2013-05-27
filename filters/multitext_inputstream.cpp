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
 * Implementation of the input stream for MultiText-compatible token streams.
 *
 * author: Stefan Buettcher
 * created: 2004-11-05
 * changed: 2005-07-21
 **/


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "multitext_inputstream.h"
#include "../misc/alloc.h"
#include "../misc/logging.h"
#include "../misc/stringtokenizer.h"


MultiTextInputStream::MultiTextInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of MultiTextInputStream(char*)


MultiTextInputStream::MultiTextInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of MultiTextInputStream(int)


void MultiTextInputStream::initialize() {	
	for (int i = 0; i <= 255; i++)
		isWhiteSpace[i] = false;
	isWhiteSpace[10] = isWhiteSpace[13] = true;
	for (int i = 0; i <= 255; i++)
		isTerminator[i] = isInstigator[i] = false;
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = 0;
	sequenceNumber = 0;
	lastProbabilitySeen = 0.0;
	InputToken batchToken;
	FilteredInputStream::getNextToken(&batchToken);
	if (strcmp((char*)batchToken.token, "batch") != 0) {
		char errorMessage[256];
		sprintf(errorMessage, "Expected: \"batch\". Found: \"%s\".", (char*)batchToken.token);
		log(LOG_ERROR, "MultiTextInputStream", errorMessage);
	}
	assert(strcmp((char*)batchToken.token, "batch") == 0);
} // end of initialize()


MultiTextInputStream::~MultiTextInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~TextInputStream()


bool MultiTextInputStream::getNextToken(InputToken *result) {
	bool success;
	unsigned int positionValue;
	double probabilityValue;
	InputToken position;
startOfGetNextToken:
	success = FilteredInputStream::getNextToken(result);
	if (!success)
		return false;
	if (result->filePosition == 0)
		return getNextToken(result);
	success = FilteredInputStream::getNextToken(&position);
	if (!success)
		return false;
	if (sscanf((char*)position.token, "%u%lf", &positionValue, &probabilityValue) == 2)
		lastProbabilitySeen = probabilityValue;
	else if (sscanf((char*)position.token, "%u", &positionValue) == 1)
		lastProbabilitySeen = 1.0;
	else
		goto startOfGetNextToken;
	sequenceNumber = positionValue + 1;
	result->sequenceNumber = positionValue;
	return true;
} // end of getNextToken(InputToken*)


double MultiTextInputStream::getLastProbabilitySeen() {
	return lastProbabilitySeen;
}


int MultiTextInputStream::getDocumentType() {
	return DOCUMENT_TYPE_MULTITEXT;
} // end of getDocumentType()


bool MultiTextInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	int lastPosition = -1;
	if (strncmp((char*)fileStart, "batch\n", 6) != 0)
		return false;
	StringTokenizer *tok = new StringTokenizer((char*)fileStart, "\n");
	tok->getNext();
	while (tok->hasNext()) {
		char *term = tok->getNext();
		if (tok->hasNext()) {
			char *position = tok->getNext();
			int newPosition;
			if (sscanf(position, "%i", &newPosition) != 1) {
				delete tok;
				return false;
			}
			else if (lastPosition >= 0) {
				if ((newPosition < lastPosition) || (newPosition > lastPosition + 1)) {
					delete tok;
					return false;
				}
			}
			lastPosition = newPosition;
		}
	}
	delete tok;
	return true;
} // end of canProcess(char*, byte*, int)



