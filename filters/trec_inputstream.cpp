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
 * The implementation of the TRECInputStream class is tuned for indexing
 * efficiency. We do not take care of special characters etc., since we do not
 * expect them to be important in TREC tasks.
 *
 * author: Stefan Buettcher
 * created: 2005-03-25
 * changed: 2006-07-16
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "trec_inputstream.h"
#include "../misc/all.h"


#define SKIP_UNCLOSED_XML_TAGS 1

static const int MAX_TAG_SKIP_LENGTH = 80;


TRECInputStream::TRECInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of TRECInputStream(char*)


TRECInputStream::TRECInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of TRECInputStream(int)


void TRECInputStream::initialize() {
	memset(translationTable, 0, sizeof(translationTable));
	memset(isNum, 0, sizeof(isNum));
	for (int i = 'a'; i <= 'z'; i++) {
		translationTable[i] = i;
		translationTable[i - 32] = i;
	}
	for (int i = '0'; i <= '9'; i++) {
		translationTable[i] = i;
		isNum[i] = 1;
	}
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	bufferStartInFile = 0;
	filePosition = 0;
	sequenceNumber = 0;
} // end of initialize()


TRECInputStream::~TRECInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~TRECInputStream()


bool TRECInputStream::reload(int *bufPos, int *bufSize) {
	assert(*bufPos > *bufSize - 1024);
	memmove(buffer, &buffer[*bufSize - 1024], 1024);
	if (filePosition == 0)
		bufferStartInFile = -1024;
	else
		bufferStartInFile += (*bufSize - 1024);
	off_t check = lseek(inputFile, 0, SEEK_CUR) - 1024;
	if (check >= 0)
		bufferStartInFile = check;
	if (bufferStartInFile < -1024)
		return false;

	bufferPos = *bufPos = 1024 - (*bufSize - *bufPos);
	*bufSize = forced_read(inputFile, &buffer[1024], BUFFER_SIZE);
	if (*bufSize <= 0) {
		bufferSize = *bufSize = 1024;
		return false;
	}
	bufferSize = *bufSize = *bufSize + 1024;
	return true;
} // end of reload(int*, int*)


static const char *STATE_TRANSLATION[128] = {
  ".ak.us", "Alaska",
  ".al.us", "Alabama",
  ".ar.us", "Arkansas",
  ".az.us", "Arizona",
  ".ca.us", "California",
  ".co.us", "Colorado",
  ".ct.us", "Connecticut",
  ".dc.us", "District of Columbia",
  ".de.us", "Delaware",
  ".fl.us", "Florida",
  ".ga.us", "Georgia",
  ".hi.us", "Hawaii",
  ".ia.us", "Iowa",
  ".id.us", "Idaho",
  ".il.us", "Illinois",
  ".in.us", "Indiana",
  ".ks.us", "Kansas",
  ".ky.us", "Kentucky",
  ".la.us", "Louisiana",
  ".ma.us", "Massachusetts",
  ".md.us", "Maryland",
  ".me.us", "Maine",
  ".mi.us", "Michigan",
  ".mn.us", "Minnesota",
  ".mo.us", "Missouri",
  ".ms.us", "Mississippi",
  ".mt.us", "Montana",
  ".nc.us", "North Carolina",
  ".nd.us", "North Dakota",
  ".ne.us", "Nebraska",
  ".nh.us", "New Hampshire",
  ".nj.us", "New Jersey",
  ".nm.us", "New Mexico",
  ".nv.us", "Nevada",
  ".ny.us", "New York",
  ".oh.us", "Ohio",
  ".ok.us", "Oklahoma",
  ".or.us", "Oregon",
  ".pa.us", "Pennsylvania",
  ".ri.us", "Rhode Island",
  ".sc.us", "South Carolina",
  ".sd.us", "South Dakota",
  ".tn.us", "Tennessee",
  ".tx.us", "Texas",
  ".ut.us", "Utah",
  ".vt.us", "Vermont",
  ".va.us", "Virginia",
  ".wa.us", "Washington",
  ".wi.us", "Wisconsin",
  ".wv.us", "West Virginia",
  ".wy.us", "Wyoming",
	NULL
};

static void printExpandedStateName(char *hostName, char *buffer) {
	int len = strlen(hostName);
	char *name;
	if (strcasecmp(&hostName[len - 3], ".us") == 0) {
		for (int i = 0; STATE_TRANSLATION[i] != NULL; i += 2) {
			if (strcasecmp(&hostName[len - 6], STATE_TRANSLATION[i]) == 0) {
				strcpy(buffer, STATE_TRANSLATION[i + 1]);
				break;
			}
		}
	}
} // printExpandedStateName(char*, char*)


bool TRECInputStream::getNextToken(InputToken *result) {
	if (inputFile < 0)
		goto return_false;

	int c, bufPos, bufSize, len;
	byte *resultToken;

	bufPos = bufferPos;
	bufSize = bufferSize;

getNextToken_START:

	// skip over whitespace characters
	translationTable[(byte)'<'] = (byte)'<';
	while (true) {
		if (bufPos >= bufSize - 1) {
			if (bufSize < BUFFER_SIZE)
				goto return_false;
			if (!reload(&bufPos, &bufSize))
				goto return_false;
		}
		if (c = translationTable[buffer[bufPos++]])
			break;
	} // end while (true)
	translationTable[(byte)'<'] = 0;

	result->canBeUsedAsLandmark = true;
	result->filePosition = bufferStartInFile + bufPos - 1;
	result->sequenceNumber = sequenceNumber;

	// read up to MAX_TOKEN_LENGTH characters into "result->token", until "<", ">",
	// or whitespace is encountered
	resultToken = result->token;
	resultToken[0] = (byte)c;
	resultToken[1] = 0;
	len = 1;

	if (c == '<') {
		translationTable[(byte)'/'] = (byte)'/';

#if 0
		// skip HTML comments
		if (bufPos >= bufSize - 4)
			if (!reload(&bufPos, &bufSize))
				goto return_false;
		if (buffer[bufPos] == '!') {
			if (strncmp((char*)&buffer[bufPos], "!--", 3) == 0) {
				while (true) {
					if (buffer[bufPos] == '-') {
						if (bufPos >= bufSize - 3)
							if (!reload(&bufPos, &bufSize))
								goto return_false;
						if (strncmp((char*)&buffer[bufPos], "-->", 3) == 0) {
							bufPos += 3;
							goto getNextToken_START;
						}
					}
					else if (buffer[bufPos] == '<') {
						if (bufPos >= bufSize - 6)
							if (!reload(&bufPos, &bufSize))
								goto return_false;
						if (strncasecmp((char*)&buffer[bufPos], "</doc>", 6) == 0)
							goto getNextToken_START;
					}
					if (++bufPos >= bufSize)
						if (!reload(&bufPos, &bufSize))
							goto return_false;
				}
				goto getNextToken_START;
			} // end if (strncmp((char*)&buffer[bufPos], "!--", 3) == 0)
		}
#endif
	} // end if (c == '<')

	if (bufPos < bufSize - MAX_TOKEN_LENGTH) {
		while (len < MAX_TOKEN_LENGTH) {
			c = translationTable[buffer[bufPos++]];
			if (c == 0) {
				if (buffer[bufPos - 1] == '>')
					resultToken[len++] = '>';
				else
					bufPos--;
				break;
			}
			else
				resultToken[len++] = (byte)c;
		} // end while (len < MAX_TOKEN_LENGTH)
	}
	else {
		while (len < MAX_TOKEN_LENGTH) {
			if (bufPos >= bufSize - 1)
				if (!reload(&bufPos, &bufSize))
					goto return_false;
			c = translationTable[buffer[bufPos++]];
			if (c == 0) {
				if (buffer[bufPos - 1] == '>')
					resultToken[len++] = '>';
				else
					bufPos--;
				break;
			}
			else
				resultToken[len++] = (byte)c;
		} // end while (len < MAX_TOKEN_LENGTH)
	}
	translationTable[(byte)'/'] = 0;

	if (resultToken[0] == (byte)'<') {
		resultToken[len] = 0;

#if SKIP_UNCLOSED_XML_TAGS

		// try to not return unclosed XML tags to the indexing system; skip input
		// characters until '>' is reached
		if (resultToken[len - 1] != (byte)'>') {
			
			// if we are too close to the end of the buffer, refill in order to avoid
			// abnormal behavior
			if (bufPos > bufSize - 512)
				reload(&bufPos, &bufSize);

			int skipLength = MAX_TAG_SKIP_LENGTH;

			// give special treatment to certain HTML tags
			char secondChar = resultToken[1];
			switch (secondChar) {
				case 'm':
					// we do not want to skip meta tags, because they tend to contain
					// interesting information
					if (strncasecmp((char*)resultToken, "<meta", 5) == 0)
						goto getNextToken_END;
					break;
				case 'd':
					// if we transform a "<doc" into a closed tag, this will confuse
					// the indexer, because it thinks this is the start of a new document,
					// when in fact it is only some annoying noise
					if (strncasecmp((char*)resultToken, "<doc", 4) == 0)
						goto getNextToken_END;
					break;
				case 't':
					// we really hate long <td> tags, because they contain no useful
					// information whatsoever, but mess up our document length values;
					// try really hard to skip them
					if (strncasecmp((char*)resultToken, "<td ", 4) == 0)
						skipLength = MAX(MAX_TAG_SKIP_LENGTH, 256);
					break;
				case 'a':
					// the same holds for <a> tags that most of the time only contain the
					// URL of some other page; while this might be useful sometimes, most
					// of the time it is not
					if (strncasecmp((char*)resultToken, "<a ", 3) == 0)
						skipLength = MAX(MAX_TAG_SKIP_LENGTH, 256);
					break;
			}

			int end = (bufSize <= bufPos + skipLength ? bufSize - 1 : bufPos + skipLength);
			int found = -1;
			for (int i = bufPos; i < end; i++) {
				byte b = buffer[i];
				if (translationTable[b] == 0) {
					if (b == (byte)'<') {
						found = i;
						break;
					}
					else {
						found = i + 1;
						if (b == (byte)'>')
							break;
						if (strncasecmp((char*)&buffer[i + 1], "alt=", 4) == 0) {
							found = i + 5;
							break;
						}
					}
				}
			} // end for (int i = bufPos; i < end; i++)

			if (found >= 0) {
				bufPos += (found - bufPos);
				if (len < MAX_TOKEN_LENGTH)
					resultToken[len++] = (byte)'>';
			}
			else
				bufPos += (end - bufPos);

		}
#endif
	} // end if (resultToken[0] == (byte)'<')

getNextToken_END:

#if 1
	if ((resultToken[0] == '<') && (resultToken[1] == 's')) {
		if ((strncmp((char*)resultToken, "<script", 7) == 0) ||
		    (strncmp((char*)resultToken, "<style", 6) == 0)) {
			while (true) {
				if (bufPos >= bufSize - 2)
					if (!reload(&bufPos, &bufSize))
						break;
				if (buffer[bufPos] == '<')
					break;
				bufPos++;
			}
		}
	}
#endif

	// ignore tokens containing numerical characters unless they are at most 4 chars long
	if ((isNum[resultToken[0]]) || (isNum[resultToken[1]])) {
		if (len > 8)
			goto getNextToken_START;
		if ((len > 7) && (resultToken[0] > '1'))
			goto getNextToken_START;
	}

	filePosition = bufferStartInFile + bufPos;
	bufferPos = bufPos;
	bufferSize = bufSize;
	resultToken[len] = 0;
	sequenceNumber++;
	return true;
return_false:
	filePosition = bufferStartInFile + bufPos;
	bufferPos = bufPos;
	bufferSize = bufSize;
	return false;
} // end of getNextToken(InputToken*)


bool TRECInputStream::seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber) {
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	bufferStartInFile = newPosition - bufferSize;
	filePosition = newPosition;
	sequenceNumber = newSequenceNumber;
	lseek(inputFile, newPosition, SEEK_SET);
	return true;
} // end of seekToFilePosition(off_t)


int TRECInputStream::getDocumentType() {
	return DOCUMENT_TYPE_TREC;
} // end of getDocumentType()


bool TRECInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < 32)
		return false;
	char *doc = strstr((char*)fileStart, "<doc>");
	if (doc == NULL)
		doc = strstr((char*)fileStart, "<DOC>");
	if ((doc == NULL) || (doc > (char*)&fileStart[200]))
		return false;
	char *docNo = strstr((char*)fileStart, "<docno>");
	if (docNo == NULL)
		if (strstr((char*)fileStart, "<DOCNO>") == NULL)
			return false;
	docNo = strstr((char*)fileStart, "</docno>");
	if (docNo == NULL)
		if (strstr((char*)fileStart, "</DOCNO>") == NULL)
			return false;
	return true;
} // end of canProcess(char*, byte*, int)



