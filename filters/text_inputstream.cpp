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
 * Implementation of the input stream for general text input.
 *
 * author: Stefan Buettcher
 * created: 2004-09-19
 * changed: 2005-11-30
 **/


#include "text_inputstream.h"
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "../misc/all.h"


static const char WHITESPACES[40] =
	{ ',', ';', '.', ':', '-', '_', '#', '\'', '+', '*', '~',
	  '°', '^', '!', '"', '§', '$', '%', '&', '/', '(', ')',
		'[', ']', '{', '}', '=', '?', '\\', '<', '>', '|', 0 };


TextInputStream::TextInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of TextInputStream(char*)


TextInputStream::TextInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of TextInputStream(int)


void TextInputStream::initialize() {
	for (int i = 0; i <= 32; i++)
		isWhiteSpace[i] = true;
	for (int i = 33; i <= 255; i++)
		isWhiteSpace[i] = false;
	for (int i = 0; WHITESPACES[i] != 0; i++)
		isWhiteSpace[(byte)WHITESPACES[i]] = true;
	for (int i = 0; i <= 255; i++)
		isTerminator[i] = isInstigator[i] = false;
	isTerminator[(byte)'@'] = true;
	isInstigator[(byte)'@'] = true;
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = 0;
	sequenceNumber = 0;
} // end of initialize()


TextInputStream::~TextInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~TextInputStream()


bool TextInputStream::getNextToken(InputToken *result) {
	bool success, onlyNumbers;
	byte *translated;
	int len, i;
getNextToken_start:
	success = FilteredInputStream::getNextToken(result);
	if (!success)
		return false;
	translated = FilteredInputStream::replaceNonStandardChars(result->token, tempString, true);
	len = strlen((char*)translated);
	if (len >= MAX_TOKEN_LENGTH)
		goto getNextToken_start;
	onlyNumbers = true;
	for (i = 0; i < len; i++) {
		if (translated[i] >= 128)
			goto getNextToken_start;
		if ((translated[i] < '0') || (translated[i] > '9'))
			onlyNumbers = false;
	}
	if ((onlyNumbers) && (len > 8))
		goto getNextToken_start;
	strcpy((char*)result->token, (char*)translated);
	return true;
} // end of getNextToken(InputToken*)


bool TextInputStream::seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber) {
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = newPosition;
	sequenceNumber = newSequenceNumber;
	lseek(inputFile, newPosition, SEEK_SET);
	return true;
} // end of seekToFilePosition(off_t, uint32_t)


int TextInputStream::getDocumentType() {
	return DOCUMENT_TYPE_TEXT;
} // end of getDocumentType()


bool TextInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < MINIMUM_LENGTH)
		return false;
	int spaceCnt = 0;
	for (int i = 0; i < length; i++)
		if ((fileStart[i] == 32) || (fileStart[i] == 8) ||
		    (fileStart[i] == 10) || (fileStart[i] == 13))
			spaceCnt++;
		else if ((fileStart[i] > 13) && (fileStart[i] < 30))
			return false;
		else if ((fileStart[i] > 0) && (fileStart[i] < 8))
			return false;
		else if (fileStart[i] == 0)
			return false;
	if ((spaceCnt >= length / 64 + 1) || (length < 128))
		return true;
	else
		return false;
} // end of canProcess(char*, byte*, int)




