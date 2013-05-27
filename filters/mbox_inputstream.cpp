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
 * Implementation of the MBoxInputStream class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-10
 * changed: 2005-03-16
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "mbox_inputstream.h"
#include "../misc/alloc.h"


static const char WHITESPACES[40] =
	{ ',', ';', '.', ':', '?', '!', '(', ')', '%', '&', '|', '-', '"', '\'', 0 };


MBoxInputStream::MBoxInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of MBoxInputStream(char*)


MBoxInputStream::MBoxInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of MBoxInputStream(int)


void MBoxInputStream::initialize() {
	for (int i = 0; i <= 32; i++)
		isWhiteSpace[i] = true;
	isWhiteSpace[(byte)'\n'] = false;
	for (int i = 33; i <= 255; i++)
		isWhiteSpace[i] = false;
	for (int i = 0; WHITESPACES[i] != 0; i++)
		isWhiteSpace[(byte)WHITESPACES[i]] = true;
	for (int i = 0; i <= 255; i++)
		isTerminator[i] = isInstigator[i] = false;
	isInstigator[(byte)'<'] = isTerminator[(byte)'>'] = true;
	isInstigator[(byte)'@'] = isTerminator[(byte)'@'] = true;
	isInstigator[(byte)'\n'] = isTerminator[(byte)'\n'] = true;
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = 0;
	sequenceNumber = 0;
	nextCallMustReturnDocTag = true;
} // end of initialize()


MBoxInputStream::~MBoxInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~MBoxInputStream()


int MBoxInputStream::getDocumentType() {
	return DOCUMENT_TYPE_MBOX;
} // end of getDocumentType()


bool MBoxInputStream::getNextToken(InputToken *result) {
getNextTokenStart:
	if (nextCallMustReturnDocTag) {
		closingDocAlreadyThere = false;
		nextCallMustReturnDocTag = false;
		result->canBeUsedAsLandmark = false;
		strcpy((char*)result->token, "<document!>");
		result->sequenceNumber = sequenceNumber;
		result->filePosition = filePosition;
		sequenceNumber++;
		return true;
	}
	if (!FilteredInputStream::getNextToken(result)) {
		if (closingDocAlreadyThere)
			return false;
		result->canBeUsedAsLandmark = false;
		result->sequenceNumber = sequenceNumber;
		result->filePosition = filePosition;
		strcpy((char*)result->token, "</document!>");
		sequenceNumber++;
		closingDocAlreadyThere = true;
		return true;
	}
	if (((byte)result->token[0]) != '\n') {
		if (((byte)result->token[0]) != '<') {
			char *slash = strchr((char*)result->token, '/');
			if (slash != NULL) {
				for (int i = 0; slash[i] != 0; i++)
					if (slash[i] == '/')
						slash[i] = ' ';
				putBackString(result->token);
				sequenceNumber--;
				goto getNextTokenStart;
			}
			byte temp[MAX_TOKEN_LENGTH * 2];
			replaceNonStandardChars(result->token, temp, true);
			if (strlen((char*)temp) >= MAX_TOKEN_LENGTH) {
				sequenceNumber--;
				goto getNextTokenStart;
			}
			strcpy((char*)result->token, (char*)temp);
		}
		return true;
	}
	sequenceNumber--;
	int c = getNextCharacter();
	if (c < 0)
		goto getNextTokenStart;
	putBackCharacter((byte)c);
	if (c == 'F') {
		int iData[8];
		byte bData[8];
		for (int i = 0; i < 5; i++) {
			iData[i] = getNextCharacter();
			bData[i] = (byte)iData[i];
		}
		bData[5] = 0;
		for (int i = 4; i >= 0; i--)
			if (iData[i] >= 0)
				putBackCharacter(bData[i]);
		if (strcmp((char*)bData, "From ") == 0) {
			if (sequenceNumber > 3) {
				strcpy((char*)result->token, "</document!>");
				result->canBeUsedAsLandmark = false;
				result->filePosition = filePosition;
				result->sequenceNumber = sequenceNumber;
				sequenceNumber++;
				nextCallMustReturnDocTag = true;
				return true;
			}
			goto getNextTokenStart;
		}
	}
	goto getNextTokenStart;
	return false;
} // end of getNextToken(InputToken*)


bool MBoxInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < MIN_MBOX_LENGTH)
		return false;
	if (strncmp((char*)fileStart, "From ", 5) != 0)
		return false;
	if (strstr((char*)fileStart, "X-UIDL: ") != NULL)
		return true;
	if (strstr((char*)fileStart, "X-Mozilla-Status: ") != NULL)
		return true;
	if (strstr((char*)fileStart, "Message-ID: ") != NULL)
		return true;
	if (strstr((char*)fileStart, "Date: ") != NULL)
		return true;
	return false;
} // end of canProcess(char*, byte*, int)



