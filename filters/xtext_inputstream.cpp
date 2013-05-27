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
 * Implementation of the input stream for the intermediate format produced by
 * XTeXT and fed into MultiText.
 *
 * author: Stefan Buettcher
 * created: 2006-10-07
 * changed: 2006-10-07
 **/


#include <string.h>
#include "xtext_inputstream.h"
#include "../misc/all.h"


XTeXTInputStream::XTeXTInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of XTeXTInputStream(char*)


XTeXTInputStream::XTeXTInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of XTeXTInputStream(int)


void XTeXTInputStream::initialize() {
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = 0;
	sequenceNumber = 0;
} // end of initialize()


XTeXTInputStream::~XTeXTInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~XTeXTInputStream()


bool XTeXTInputStream::getNextToken(InputToken *result) {
	int status, tokenLen;
getNextToken_START:
	status = getNextCharacter();
	tokenLen = 0;
	switch (status) {
		case '+':  // proper word; put into even index address
		case '-':  // annotation symbol; put into odd index address
			if (status == '-')
				sequenceNumber = (sequenceNumber | 1);
			else
				sequenceNumber = (sequenceNumber | 1) + 1;
			result->sequenceNumber = sequenceNumber;
			result->filePosition = filePosition - 1;
			while ((status = getNextCharacter()) != '\n') {
				if (status < 0)
					break;
				result->token[tokenLen++] = (byte)status;
				assert(tokenLen <= MAX_TOKEN_LENGTH);
			}
			result->token[tokenLen] = 0;
			return true;
		default:  // text; skip until end of line
			while ((status = getNextCharacter()) != '\n')
				if (status < 0)
					return false;
	}
	goto getNextToken_START;
	return false;
} // end of getNextToken(InputToken*)


int XTeXTInputStream::getDocumentType() {
	return DOCUMENT_TYPE_XTEXT;
} // end of getDocumentType()


bool XTeXTInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < MINIMUM_LENGTH)
		return false;
	if (strncmp((char*)fileStart, "-<append>", 9) == 0)
		if (strncmp((char*)&fileStart[10], "-<batch>", 8) == 0)
			return true;
	return false;
} // end of canProcess(char*, byte*, int)




