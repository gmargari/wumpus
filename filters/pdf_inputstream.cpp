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
 * Implementation of PDFInputStream class. See the header file for documentation.
 *
 * author: Stefan Buettcher
 * created: 2004-09-29
 * changed: 2006-08-29
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "pdf_inputstream.h"
#include "../indexcache/documentcache.h"
#include "../misc/all.h"


static const char *PDFTOTEXT = "pdftotext";

static const char WHITESPACES[40] =
	{ ',', ';', '.', ':', '-', '_', '#', '\'', '+', '*', '~',
		'°', '^', '!', '"', '§', '$', '%', '&', '/', '(', ')',
		'[', ']', '{', '}', '=', '?', '\\', '<', '>', '|', 0 };


void PDFInputStream::initialize(const char *fileName, DocumentCache *cache) {
	for (int i = 0; i <= 32; i++)
		isWhiteSpace[i] = true;
	isWhiteSpace[12] = false;
	for (int i = 33; i <= 255; i++)
		isWhiteSpace[i] = false;
	for (int i = 0; WHITESPACES[i] != 0; i++)
		isWhiteSpace[WHITESPACES[i]] = true;
	for (int i = 0; i <= 255; i++)
		isTerminator[i] = isInstigator[i] = false;
	isTerminator[12] = isInstigator[12] = true;
	sprintf(tempFileName, "%s/%s", TEMP_DIRECTORY, "index-conversion-XXXXXXXX.txt");
	randomTempFileName(tempFileName);

	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = 0;
	sequenceNumber = 0;

	statusCode = -1;
	if (getTextFromCache(cache, tempFileName))
		statusCode = 0;

	if (fileName == NULL)
		goto initialize_EXIT;

	if (statusCode != 0) {
		int convTime = currentTimeMillis();
		statusCode = executeCommand((char*)PDFTOTEXT,
				"-enc", "Latin1", fileName, tempFileName, INPUT_CONVERSION_TIMEOUT); 
		if ((statusCode == 0) && (cache != NULL)) {
			convTime = currentTimeMillis() - convTime;
			if (convTime < 0)
				convTime += 24 * 3600 * 1000;
			cache->addDocumentTextFromFile(originalFileName, tempFileName, convTime);
		}
	}

initialize_EXIT:

	if (statusCode != 0) {
		inputFile = -1;
		unlink(tempFileName);
	}
	else
		inputFile = open(tempFileName, O_RDONLY);
} // end of initialize(char*, DocumentCache*)


PDFInputStream::PDFInputStream() {
	originalFileName = NULL;
	inputFile = -1;
} // end of PDFInputStream()


PDFInputStream::PDFInputStream(const char *fileName) {
	originalFileName = duplicateString(fileName);
	PDFInputStream::initialize(fileName, NULL);
}


PDFInputStream::PDFInputStream(const char *fileName, DocumentCache *cache) {
	originalFileName = duplicateString(fileName);
	PDFInputStream::initialize(fileName, cache);
}


PDFInputStream::~PDFInputStream() {
	if (inputFile > 0) {
		close(inputFile);
		inputFile = -1;
	}
	if (originalFileName != NULL) {
		free(originalFileName);
		originalFileName = NULL;
	}
	unlink(tempFileName);
} // end of ~PDFInputStream()


bool PDFInputStream::getNextToken(InputToken *result) {
	if (inputFile < 0)
		return false;
	if ((filePosition == 0) && (sequenceNumber == 0)) {
		closingDocWasThere = false;
		strcpy((char*)result->token, "<document!>");
		result->sequenceNumber = sequenceNumber;
		result->filePosition = filePosition;
		result->canBeUsedAsLandmark = false;
		sequenceNumber++;
		return true;
	}
	bool success;
pdf_getNextToken_start:
	success = FilteredInputStream::getNextToken(result);
	if (!success) {
		if (closingDocWasThere)
			return false;
		closingDocWasThere = true;
		strcpy((char*)result->token, "</document!>");
		result->sequenceNumber = sequenceNumber;
		result->filePosition = filePosition;
		result->canBeUsedAsLandmark = false;
		sequenceNumber++;
		return true;
	}
	if ((result->token[0] == 12) && (result->token[1] == 0)) {
		int nextChar = getNextCharacter();
		if (nextChar >= 0)
			putBackCharacter(nextChar);
		if (nextChar >= 32)
			goto pdf_getNextToken_start;
		strcpy((char*)result->token, "<newpage/>");
		return true;
	}
	byte *translated = FilteredInputStream::replaceNonStandardChars(result->token, tempString, true);
	int len = strlen((char*)translated);
	if (len > MAX_TOKEN_LENGTH)
		translated[MAX_TOKEN_LENGTH] = 0;
	strcpy((char*)result->token, (char*)translated);
	return true;
} // end of getNextToken(InputToken*)


bool PDFInputStream::getTextFromCache(DocumentCache *cache, const char *tempFileName) {
	if ((cache == NULL) || (tempFileName == NULL))
		return false;
	int size;
	char *buffer = cache->getDocumentText(originalFileName, &size);
	if (buffer != NULL) {
		int fd = open(tempFileName, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_FILE_PERMISSIONS);
		if (fd >= 0) {
			forced_write(fd, buffer, size);
			close(fd);
			free(buffer);
			return true;
		}
		free(buffer);
	}
	return false;
} // end of getTextFromCache(DocumentCache*, char*)


int PDFInputStream::getDocumentType() {
	return DOCUMENT_TYPE_PDF;
} // end of getDocumentType()


bool PDFInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < MIN_PDF_SIZE)
		return false;
	if (strncmp((char*)fileStart, "%PDF-1.", 7) == 0)
		return true;
	return false;
} // end of canProcess(char*, byte*, int)



