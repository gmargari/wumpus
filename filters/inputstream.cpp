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
 * Implementation of the generic input filter (FilteredDocument, FilteredInputStream).
 *
 * author: Stefan Buettcher
 * created: 2004-09-07
 * changed: 2009-02-01
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "inputstream.h"
#include "bzip2_inputstream.h"
#include "gzip_inputstream.h"
#include "html_inputstream.h"
#include "office_inputstream.h"
#include "pdf_inputstream.h"
#include "ps_inputstream.h"
#include "mbox_inputstream.h"
#include "mp3_inputstream.h"
#include "ngram_inputstream.h"
#include "text_inputstream.h"
#include "trec_inputstream.h"
#include "trecmulti_inputstream.h"
#include "troff_inputstream.h"
#include "xml_inputstream.h"
#include "multitext_inputstream.h"
#include "xtext_inputstream.h"
#include "../index/index.h"
#include "../indexcache/documentcache.h"
#include "../misc/all.h"


static const char * LOG_ID = "FilteredInputStream";

static const char translationTable[40] =
	{ 'Á', 'À', 'Â', 'Ä',
		'á', 'à', 'â', 'ä',
		'é', 'è', 'ê',
		'í', 'ì', 'î',
		'Ó', 'Ò', 'Ô', 'Ö',
		'ó', 'ò', 'ô', 'ö',
		'Ú', 'Ù', 'Û', 'Ü',
		'ú', 'ù', 'û', 'ü',
		'ý', 'ß', 0 };

static const char *translationTarget[40] =
	{ "A", "A", "A", "Ae",
		"a", "a", "a", "ae",
		"e", "e", "e",
		"i", "i", "i",
		"O", "O", "O", "Oe",
		"o", "o", "o", "oe",
		"U", "U", "U", "Ue",
		"u", "u", "u", "ue",
		"y", "ss", NULL };

static int translationLength[256];

static const char *translation[256];

static bool translationInitialized = false;

const char * FilteredInputStream::TEMP_DIRECTORY = "/tmp";

const char * FilteredInputStream::DOCUMENT_TYPES[MAX_DOCUMENT_TYPE + 2] = {
	NULL,
	"text/html",
	"application/x-office",
	"application/pdf",
	"application/postscript",
	"text/plain",
	"text/xml",
	"text/x-mail",
	"application/multitext",
	"audio/mpeg",
	"text/x-trec",
	"text/x-trec-multi",
	"text/xtext",
	"text/troff",
	NULL
};


FilteredInputStream::FilteredInputStream() {
	inputFile = -1;
	mustUseSmallBuffer = false;
} // end of FilteredInputStream()


FilteredInputStream::FilteredInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of FilteredInputStream(char*)


FilteredInputStream::FilteredInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of FilteredInputStream(int)


FilteredInputStream::FilteredInputStream(char *inputString, int inputLength) {
	inputFile = -1;
	initialize();
	bufferSize = inputLength;
	if (bufferSize >= BUFFER_SIZE)
		bufferSize = BUFFER_SIZE - 1;
	bufferPos = 0;
	strncpy((char*)buffer, inputString, bufferSize);
} // end of FilteredInputStream(char*, int)


void FilteredInputStream::initialize() {
	for (int i = 0; i <= 32; i++)
		isWhiteSpace[i] = true;
	for (int i = 33; i <= 255; i++)
		isWhiteSpace[i] = false;
	for (int i = 0; i <= 255; i++)
		isTerminator[i] = isInstigator[i] = false;
	bufferSize = BUFFER_SIZE;
	bufferPos = 2 * BUFFER_SIZE;
	mustUseSmallBuffer = false;
	filePosition = 0;
	sequenceNumber = 0;
} // end of initialize()


FilteredInputStream * FilteredInputStream::getInputStream(
		const char *fileName, DocumentCache *cache) {
	if (fileName == NULL)
		return NULL;
	if (fileName[0] == 0)
		return NULL;
	int fileHandle = open(fileName, O_RDONLY);
	if (fileHandle < 0)
		return NULL;
	byte buffer[2048];
	int bufferSize = forced_read(fileHandle, buffer, 2047);
	close(fileHandle);
	if (bufferSize > 0)
		buffer[bufferSize] = 0;
	else
		return NULL;
	int documentType = getDocumentType(fileName, buffer, bufferSize);
	if (documentType <= 0)
		return NULL;
	else
		return getInputStream(fileName, documentType, cache);
} // end of getInputStream(char*)


FilteredInputStream * FilteredInputStream::getInputStream(
		const char *fileName, int documentType, DocumentCache *cache) {
	if (documentType < 0)
		return getInputStream(fileName, cache);

	FilteredInputStream *result = NULL;
	if (documentType == DOCUMENT_TYPE_HTML)
		result = new HTMLInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_OFFICE)
		result = new OfficeInputStream(fileName, cache);
	else if (documentType == DOCUMENT_TYPE_PDF)
		result = new PDFInputStream(fileName, cache);
	else if (documentType == DOCUMENT_TYPE_PS)
		result = new PSInputStream(fileName, cache);
	else if (documentType == DOCUMENT_TYPE_MBOX)
		result = new MBoxInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_MPEG)
		result = new MP3InputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_TEXT)
		result = new TextInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_XML)
		result = new XMLInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_MULTITEXT)
		result = new MultiTextInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_TREC)
		result = new TRECInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_TRECMULTI)
		result = new TRECMultiInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_TROFF)
		result = new TroffInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_GZIP)
		result = new GZIPInputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_BZIP2)
		result = new BZIP2InputStream(fileName);
	else if (documentType == DOCUMENT_TYPE_XTEXT)
		result = new XTeXTInputStream(fileName);

	if (result == NULL) {
		char errorMessage[256];
		sprintf(errorMessage, "Unable to create input stream for file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		return NULL;
	}

	// Check whether we want to index n-grams instead of individual tokens.
	bool useNGramTokenizer;
	getConfigurationBool("USE_NGRAM_TOKENIZER", &useNGramTokenizer, false);
	if (useNGramTokenizer)
		return new NGramInputStream(result, NGramInputStream::TAKE_OWNERSHIP);
	else
		return result;
} // end of getInputStream(char*, int, DocumentCache*)


FilteredInputStream::~FilteredInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~FilteredInputStream()


void FilteredInputStream::useSmallBuffer() {
	mustUseSmallBuffer = true;
}


byte * FilteredInputStream::replaceNonStandardChars(byte *oldString, byte *newString, bool toLowerCase) {
	if (!translationInitialized) {
		for (int i = 0; i < 256; i++) {
			translation[i] = NULL;
			translationLength[i] = 0;
		}
		for (int i = 0; translationTable[i] != 0; i++) {
			byte b = (byte)translationTable[i];
			translation[b] = translationTarget[i];
			translationLength[b] = strlen(translation[b]);
		}
		translationInitialized = true;
	}
	if (newString == NULL)
		newString = (byte*)malloc(strlen((char*)oldString) * 2 + 4);
	int inPos = 0;
	int outPos = 0;
	while (oldString[inPos] != 0) {
		const char *t = translation[oldString[inPos]];
		if (t == NULL)
			newString[outPos++] = oldString[inPos];
		else
			for (int i = 0; t[i] != 0; i++)
				newString[outPos++] = t[i];
		inPos++;
		if (outPos > MAX_TOKEN_LENGTH)
			break;
	}
	newString[outPos] = 0;
	if (toLowerCase)
		for (int i = 0; newString[i] != 0; i++)
			if ((newString[i] >= 'A') && (newString[i] <= 'Z'))
				newString[i] += 32;
	return newString;
} // end of replaceNonStandardChars(byte*, byte*, bool)


bool FilteredInputStream::getNextToken(InputToken *result) {
	byte *token;
	int tokenLen, c;
	bool onlyNumbers;

	result->canBeUsedAsLandmark = true;
	int bufPos = bufferPos;
	int bufSize = bufferSize;

FIS_getNextToken_start:

	// initialize result structure
	token = result->token;
	result->sequenceNumber = sequenceNumber;

	// check for EOF	
	tokenLen = 0;
	if (bufPos < bufSize) {
		filePosition++;
		c = buffer[bufPos++];
	}
	else {
		bufferPos = bufPos;
		c = getNextCharacter();
		bufPos = bufferPos;
		bufSize = bufferSize;
		if ((c < 0) || (c > 255))
			return false;
	}

	// skip leading spaces
	while (isWhiteSpace[c]) {
		if (bufPos < bufSize) {
			filePosition++;
			c = buffer[bufPos++];
		}
		else {
			bufferPos = bufPos;
			c = getNextCharacter();
			bufPos = bufferPos;
			bufSize = bufferSize;
			if (c < 0)
				return false;
		}
	} // end while (isWhiteSpace[c])

	// "filePosition - 1" because we have already read one character
	result->filePosition = filePosition - 1;

	// read until MAX_TOKEN_LENGTH or end of token has been reached
	token[tokenLen++] = (byte)c;
	while (!isTerminator[c]) {
		if (bufPos < bufSize) {
			filePosition++;
			c = buffer[bufPos++];
		}
		else {
			bufferPos = bufPos;
			c = getNextCharacter();
			bufPos = bufferPos;
			bufSize = bufferSize;
			if ((c < 0) || (c > 255))
				break;
		}
		if (isWhiteSpace[c])
			break;
		if (isInstigator[c]) {
			if (bufPos > 0) {
				buffer[--bufPos] = (byte)c;
				filePosition--;
			}
			else {
				bufferPos = bufPos;
				putBackCharacter(c);
				bufPos = bufferPos;
				bufSize = bufferSize;
			}
			break;
		}
		token[tokenLen] = (byte)c;
		if (++tokenLen >= MAX_TOKEN_LENGTH)
			break;
	}

	// resync with instance variable
	bufferPos = bufPos;

	token[tokenLen] = 0;
	sequenceNumber++;
	return true;
} // end of getNextToken(char*)


int FilteredInputStream::getNextN(InputToken **result, int maxCount) {
	int count = 0;
	while (count < maxCount) {
		if (!getNextToken(result[count]))
			break;
		else
			count++;
	}
	return count;
} // end of getNextN(InputToken**, int)


void FilteredInputStream::putBackCharacter(byte character) {
	if (bufferPos > 0)
		// this part is easy: just decrement the pointer
		buffer[--bufferPos] = character;
	else {
		// "tricky" part: move the entire array and adjust the file pointer
		if (bufferSize == BUFFER_SIZE) {
			memmove(&buffer[64], &buffer[0], BUFFER_SIZE - 64);
			bufferPos = 63;
			buffer[bufferPos] = character;
			if (inputFile >= 0)
				lseek(inputFile, -64, SEEK_CUR);
		}
		else {
			int relocation = BUFFER_SIZE - bufferSize;
			memmove(&buffer[relocation], &buffer[0], BUFFER_SIZE - relocation);
			bufferSize = BUFFER_SIZE;
			bufferPos = relocation - 1;
			buffer[bufferPos] = character;
		}
	}
	filePosition--;
} // end of putBackCharacter(byte)


void FilteredInputStream::putBackString(byte *string) {
	int pos = strlen((char*)string) - 1;
	while (pos >= 0) {
		putBackCharacter(string[pos]);
		pos--;
	}
} // end of putBackString(byte*)


int FilteredInputStream::getNextCharacter() {
	if (bufferPos < bufferSize) {
		filePosition++;
		return buffer[bufferPos++];
	}
	else {
		if (inputFile >= 0) {
			memmove(buffer, &buffer[BUFFER_SIZE - 1024], 1024);
			bufferPos = 1024;
			if (mustUseSmallBuffer) {
				// this most likely happens for read operations that are part of an
				// @get query, since in that case we only want to read data that we
				// actually need
				bufferSize = forced_read(inputFile, &buffer[bufferPos], SMALL_BUFFER_SIZE);
			}
			else if (bufferPos > bufferSize) {
				// no idea why I introduced this case
				bufferSize = forced_read(inputFile, &buffer[bufferPos], BUFFER_SIZE / 2);
			}
			else {
				// default case: simply fill the entire read buffer
				bufferSize = forced_read(inputFile, &buffer[bufferPos], BUFFER_SIZE);
			}
			if (bufferSize <= 0)
				bufferSize = 0;
			else
				bufferSize += bufferPos;
		}
		else
			bufferSize = 0;
		if (bufferSize == 0)
			return -1;
		else {
			filePosition++;
			return buffer[bufferPos++];
		}
	}
} // end of getNextCharacter()


char * FilteredInputStream::getRange(uint32_t startToken, uint32_t endToken,
		TokenPositionPair *positions, int *length, int *tokenCount) {

	uint32_t startTok = 0;
	off_t startPos = 0;
	if (positions != NULL) {
		for (int i = 0; positions[i].sequenceNumber != 0; i++)
			if (positions[i].sequenceNumber <= startToken) {
				startTok = positions[i].sequenceNumber;
				startPos = positions[i].filePosition;
			}
	}

	sequenceNumber = startTok;
	lseek(inputFile, startPos, SEEK_SET);
	filePosition = startPos;
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	InputToken token;

	off_t firstPosition = -1;
	off_t lastPosition = -1;
	*tokenCount = 0;

	while (true) {
		off_t thisFilePosition = filePosition;
		if (!getNextToken(&token)) {
			lastPosition = thisFilePosition - 1;
			break;
		}
		if ((token.sequenceNumber >= startToken) && (firstPosition < 0))
			firstPosition = token.filePosition;
		if (firstPosition >= 0) {
			if ((token.sequenceNumber > endToken) ||
			    (token.filePosition > firstPosition + MAX_FILTERED_RANGE_SIZE)) {
				lastPosition = token.filePosition - 1;
				break;
			}
		}
		if (token.sequenceNumber >= startToken)
			*tokenCount++;
	}

	if (lastPosition < firstPosition)
		lastPosition = firstPosition - 1;
	if (lastPosition > firstPosition + MAX_FILTERED_RANGE_SIZE - 1024)
		lastPosition = firstPosition + MAX_FILTERED_RANGE_SIZE - 1024;

	char *result = (char*)malloc(lastPosition - firstPosition + 2);
	lseek(inputFile, (off_t)firstPosition, SEEK_SET);
	int readResult = forced_read(inputFile, result, lastPosition - firstPosition + 1);
	if (readResult < 0)
		readResult = 0;
	result[readResult] = 0;

	// replace '\0' characters in the output buffer by newlines; it is unfortunate
	// that we have to do this, but my crappy TCP interface makes it necessary...
	for (int i = 0; i < readResult; i++)
		if (result[i] == 0)
			result[i] = '\n';

	struct stat buf;
	fstat(inputFile, &buf);
	lseek(inputFile, buf.st_size, SEEK_SET);

	*length = readResult;
	if (readResult < MAX_TOKEN_LENGTH)
		result[readResult] = 0;
	return result;
} // end of getRange(...)


char * FilteredInputStream::getFilteredRange(uint32_t startToken,
		uint32_t endToken, TokenPositionPair *positions, int *length, int *tokenCount) {

	uint32_t startTok = 0;
	off_t startPos = 0;
	if (positions != NULL) {
		for (int i = 0; positions[i].sequenceNumber != 0; i++)
			if (positions[i].sequenceNumber <= startToken) {
				startTok = positions[i].sequenceNumber;
				startPos = positions[i].filePosition;
			}
	}

	int allocatedLength = 8192;
	int usedLength = 0;
	char *result = typed_malloc(char, allocatedLength);
	result[0] = 0;

	sequenceNumber = startTok;
	lseek(inputFile, startPos, SEEK_SET);
	filePosition = startPos;
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	InputToken token;

	while (sequenceNumber <= endToken) {
		if (!getNextToken(&token))
			break;
		if (token.sequenceNumber < startToken)
			continue;
		int tokenLength = strlen((char*)token.token);
		if (usedLength + tokenLength >= allocatedLength - 2) {
			if (allocatedLength >= MAX_FILTERED_RANGE_SIZE)
				break;
			allocatedLength = (int)(allocatedLength * 1.41);
			result = typed_realloc(char, result, allocatedLength);
			assert(result != NULL);
		}
		if (usedLength > 0)
			result[usedLength++] = ' ';
		strcpy(&result[usedLength], (char*)token.token);
		usedLength += tokenLength;
	}

	*length = usedLength;
	*tokenCount = sequenceNumber - startToken;
	return result;
} // end of getFilteredRange(...)


int FilteredInputStream::getDocumentType() {
	return DOCUMENT_TYPE_GENERAL;
} // end of getDocumentType()


int FilteredInputStream::getDocumentType(const char *fileName, byte *fileStart, int length) {
	if (length < MINIMUM_LENGTH)
		return DOCUMENT_TYPE_UNKNOWN;
	if (PDFInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_PDF;
	if (PSInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_PS;
	if (MP3InputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_MPEG;
	if (TRECMultiInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_TRECMULTI;
	if (TRECInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_TREC;
	if (HTMLInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_HTML;
	if (MBoxInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_MBOX;
	if (MultiTextInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_MULTITEXT;
	if (OfficeInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_OFFICE;
	if (GZIPInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_GZIP;
	if (BZIP2InputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_BZIP2;
	if (XTeXTInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_XTEXT;
	if (TroffInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_TROFF;
	if (XMLInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_XML;
	if (TextInputStream::canProcess(fileName, fileStart, length))
		return DOCUMENT_TYPE_TEXT;
	return DOCUMENT_TYPE_UNKNOWN;
} // end of getDocumentType(byte*, int)


char * FilteredInputStream::documentTypeToString(int docType) {
	if ((docType <= 0) || (docType > MAX_DOCUMENT_TYPE))
		return duplicateString("application/unknown");
	else if (DOCUMENT_TYPES[docType] == NULL)
		return duplicateString("application/unknown");
	else
		return duplicateString(DOCUMENT_TYPES[docType]);
} // end of documentTypeToString(int)


int FilteredInputStream::stringToDocumentType(const char *docTypeString) {
	if (docTypeString == NULL)
		return -1;
	for (int i = 0; i <= MAX_DOCUMENT_TYPE; i++)
		if (DOCUMENT_TYPES[i] != NULL)
			if (strcasecmp(docTypeString, DOCUMENT_TYPES[i]) == 0)
				return i;
	return -1;
} // end of stringToDocumentType(char*)


int FilteredInputStream::getFileHandle() {
	return inputFile;
}


void FilteredInputStream::getPreviousChars(char *buffer, int bufferSize) {
	int start = bufferPos - bufferSize;
	if (start < 0)
		start = 0;
	int length = bufferPos - start;
	for (int i = 0; i < length; i++)
		buffer[i] = (char)this->buffer[start + i];
	for (int i = length; i < bufferSize; i++)
		buffer[i] = ' ';
} // end of getPreviousChars(char*, int)


bool FilteredInputStream::seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber) {
	return false;
}


//#define INPUTFILTER_DEBUG

#ifdef INPUTFILTER_DEBUG

int main(int argc, char **argv) {
	InputToken token;
	for (int i = 1; i < argc; i++) {
		printf("[%s]\n", argv[i]);
		FilteredInputStream *fis = new TextInputStream(argv[i]);
		while (fis->getNextToken(&token))
			printf("%s\n", token.token);
		delete fis;
	}
	return 0;
} // end of main()

#endif


