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
 * Implementation of the input stream for XML input.
 *
 * author: Stefan Buettcher
 * created: 2004-09-25
 * changed: 2009-02-01
 **/


#include "xml_inputstream.h"
#include "text_inputstream.h"
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "../misc/all.h"
#include "../misc/configurator.h"


/** A list of all characters we consider whitespace. **/
static const char WHITESPACES[32] =
	{ ',', '.', ':', '-', '_', '#', '\'', '+', '*', '~',
	  '°', '^', '"', '§', '$', '%', '(', ')', '/', '!',
		'[', ']', '{', '}', '=', '?', '\\', '|', 0 };


XMLInputStream::XMLInputStream() {
	XMLInputStream::initialize();
}


XMLInputStream::XMLInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	XMLInputStream::initialize();
} // end of XMLInputStream(char*)


XMLInputStream::XMLInputStream(int fd) {
	inputFile = fd;
	XMLInputStream::initialize();
} // end of XMLInputStream(int)


XMLInputStream::XMLInputStream(char *inputString, int inputLength, bool atQueryTime) {
	inputFile = -1;
	XMLInputStream::initialize();
	if (atQueryTime) {
		isWhiteSpace[(byte)'$'] = false;
		isWhiteSpace[(byte)'*'] = false;
		isWhiteSpace[(byte)'?'] = false;
	}
	bufferSize = inputLength;
	if (bufferSize >= BUFFER_SIZE)
		bufferSize = BUFFER_SIZE - 1;
	bufferPos = 0;
	strncpy((char*)buffer, inputString, bufferSize);			 
	this->atQueryTime = atQueryTime;
} // end of XMLInputStream(char*, int)


void XMLInputStream::initialize() {
	char xmlComments[MAX_CONFIG_VALUE_LENGTH];
	xmlCommentMode = COMMENTS_DEFAULT;
	if (getConfigurationValue("XML_COMMENTS", xmlComments)) {
		if (strcasecmp(xmlComments, "plaintext") == 0)
			xmlCommentMode = COMMENTS_PLAINTEXT;
		else if (strcasecmp(xmlComments, "ignore") == 0)
			xmlCommentMode = COMMENTS_IGNORE;
	}
	for (int i = 0; i <= 32; i++)
		isWhiteSpace[i] = true;
	for (int i = 33; i <= 255; i++)
		isWhiteSpace[i] = false;
	for (int i = 0; WHITESPACES[i] != 0; i++)
		isWhiteSpace[(byte)WHITESPACES[i]] = true;
	for (int i = 0; i <= 255; i++)
		isTerminator[i] = isInstigator[i] = false;
	isInstigator[(byte)'<'] = true;
	isInstigator[(byte)'>'] = isTerminator[(byte)'>'] = true;
	isTerminator[(byte)'@'] = true;
	isInstigator[(byte)'@'] = true;
	bufferSize = BUFFER_SIZE;
	bufferPos = BUFFER_SIZE;
	filePosition = 0;
	sequenceNumber = 0;
	termQueueLength = 0;
	currentTag[0] = 0;
	currentlyInComment = false;
	atQueryTime = false;
} // end of initialize()


XMLInputStream::~XMLInputStream() {
	if (inputFile > 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~XMLInputStream()


static const char *entityRefs[128] = {
	// Latin references
	"nbsp", "iexcl", "Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring",
	"AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute",
	"Icirc", "Iuml", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml",
	"Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "szlig", "aacute",
	"acirc", "atilde", "auml", "aring", "aelig", "ccedil", "egrave", "eacute",
	"ecirc", "euml", "igrave", "iacute", "icirc", "iuml", "ntilde", "ograve",
	"oacute", "ocirc", "otilde", "ouml", "oslash", "ugrave", "uacute", "ucirc",
	"uuml", "yacute", "yuml", "Oelig", "oelig", "Scaron", "scaron", "euro",
	// Greek references
	"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota",
	"Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma", "Tau",
	"Upsilon", "Phi", "Chi", "Psi", "Omega",
	"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota",
	"kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau",
	"upsilon", "phi", "chi", "psi", "omega", "thetasym", "upsih", "piv",
	NULL
};

static const char *entityVals[128] = {
	" ", " ", "A", "A", "A", "A", "Ae", "A",
	"Ae", "C", "E", "E", "E", "E", "I", "I",
	"I", "I", "N", "O", "O", "O", "O", "Oe",
	"Oe", "U", "U", "U", "Ue", "Y", "ss", "a",
	"a", "a", "ae", "a", "ae", "c", "e", "e",
	"e", "e", "i", "i", "i", "i", "n", "o",
	"o", "o", "o", "oe", "oe", "u", "u", "u",
	"ue", "y", "y", "Oe", "oe", "S", "s", "Euro",
	"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Iota",
	"Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma", "Tau",
	"Upsilon", "Phi", "Chi", "Psi", "Omega",
	"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta", "iota",
	"kappa", "lambda", "mu", "nu", "xi", "omicron", "pi", "rho", "sigma", "tau",
	"upsilon", "phi", "chi", "psi", "omega", "theta", "upsilon", "pi",
	NULL
};


byte * XMLInputStream::replaceEntityReferences(byte *oldString, byte *newString) {
	if (newString == NULL)
		newString = (byte*)malloc(strlen((char*)oldString) + 4);
	int outPos = 0;
	for (int inPos = 0; oldString[inPos] != 0; inPos++) {
		if (oldString[inPos] == '&') {
			int semiPos = -1;
			for (int i = inPos; oldString[i] > 32; i++)
				if (oldString[i] == ';') {
					semiPos = i;
					break;
				}
			if ((semiPos < 0) || (semiPos > inPos + 9))
				newString[outPos++] = ' ';
			else {
				bool found = false;
				for (int i = 0; entityRefs[i] != NULL; i++) {
					int len = strlen(entityRefs[i]);
					if ((strncmp((char*)&oldString[inPos + 1], entityRefs[i], len) == 0) &&
					    (oldString[inPos + 1 + len] == ';')) {
						for (int k = 0; entityVals[i][k] != 0; k++)
							newString[outPos++] = entityVals[i][k];
						found = true;
					}
				}
				if (!found)
					newString[outPos++] = ' ';
				inPos = semiPos;
			}
		} // end if (oldString[inPos] == '&')
		else
			newString[outPos++] = oldString[inPos];
	}
	newString[outPos] = 0;
	return newString;
} // end of replaceEntityReferences(byte*)


void XMLInputStream::addToTermQueue(InputToken *token) {
	if (termQueueLength >= MAX_QUEUE_LENGTH)
		return;
	memcpy(&termQueue[termQueueLength], token, sizeof(InputToken));
	termQueue[termQueueLength].canBeUsedAsLandmark = false;
	termQueueLength++;
} // end of addToTermQueue(InputToken*)


bool XMLInputStream::getNextToken(InputToken *result) {
	// remember current sequence number; this allows us to call 
	// FilteredInputStream::getNextToken(), which changes the sequence number
	uint32_t oldSequenceNumber = sequenceNumber;

	// if there are tokens in the queue, return them first before parsing any
	// input; make sure we are not at query time -- returning multiple tokens
	// with the same sequence number would confuse the query processor
	if ((termQueueLength > 0) && (!atQueryTime)) {
		memcpy(result, &termQueue[0], sizeof(InputToken));
		termQueueLength--;
		for (int i = 0; i < termQueueLength; i++)
			termQueue[i] = termQueue[i + 1];
		sequenceNumber = result->sequenceNumber + 1;
		return true;
	}

getNextToken_START:

	while (true) {
		// skip over whitespace characters
		int nextChar;
		do {
			if (bufferPos < bufferSize) {
				filePosition++;
				nextChar = buffer[bufferPos++];
			}
			else if ((nextChar = getNextCharacter()) < 0)
				return false;
		} while (isWhiteSpace[nextChar]);

		// Insert special handling for XML tags: we don't want to break them
		// up even if they contain underscores, slashes, etc.
		if (nextChar == '<') {
			result->filePosition = filePosition - 1;
			int len = 1;
			result->token[0] = '<';
			while (len < MAX_TOKEN_LENGTH) {
				if (bufferPos < bufferSize) {
					filePosition++;
					nextChar = buffer[bufferPos++];
				}
				else if ((nextChar = getNextCharacter()) < 0)
					break;
				if ((nextChar <= ' ') || ((len > 1) && (nextChar == '/'))) {
					putBackCharacter((byte)nextChar);
					break;
				}
				if ((nextChar >= 'A') && (nextChar <= 'Z'))
					nextChar += 32;
				result->token[len++] = (byte)nextChar;
				if (nextChar == '>')
					break;
			}
			result->token[len] = 0;

			result->canBeUsedAsLandmark = false;
			result->sequenceNumber = sequenceNumber = oldSequenceNumber;

			if (currentlyInComment) {
				int in, out;
				switch (xmlCommentMode) {
					case COMMENTS_PLAINTEXT:
						for (in = 0, out = 0; result->token[in] != 0; in++) {
							char c = (char)result->token[in];
							if ((c != '<') && (c != '>'))
								result->token[out++] = (byte)c;
						}
						currentTag[0] = 0;
						if (out == 0)
							goto getNextToken_START;
						result->token[out] = 0;
						goto getNextToken_RETURN;
					case COMMENTS_IGNORE:
						currentTag[0] = 0;
						goto getNextToken_START;
				}
				result->canBeUsedAsLandmark = false;
			} // end if (currentlyInComment)

			// check whether this token is an unclosed opening XML tag; if so, add '>'
			// and put the new token into the term queue
			currentTag[0] = 0;
			if (result->token[len - 1] != '>') {
				if ((len < MAX_TOKEN_LENGTH) && (result->token[1] != '!')) {
					InputToken dummy;
					memcpy(&dummy, result, sizeof(InputToken));
					dummy.token[len] = '>';
					dummy.token[len + 1] = 0;
					if (result->token[1] != '/')
						addToTermQueue(&dummy);
					if (len < MAX_TOKEN_LENGTH - 2)
						strcpy(currentTag, (char*)&result->token[1]);
					else
						currentTag[0] = 0;
				}
			} // end if (result->token[len - 1] != '>')

			if (strcmp((char*)result->token, "<!--") == 0) {
				currentlyInComment = true;
				currentTag[0] = 0;
				if (xmlCommentMode == COMMENTS_IGNORE)
					strcpy((char*)result->token, "<!-->");
			}

getNextToken_RETURN:
			sequenceNumber++;
			return true;
		} // end if (nextChar == '<')

		// if we get here, that means that the current token is not the start
		// of an XML tag; put "nextChar" back into the read buffer
		if (bufferPos > 0) {
			buffer[--bufferPos] = (byte)nextChar;
			filePosition--;
		}
		else
			putBackCharacter((byte)nextChar);

		bool success = FilteredInputStream::getNextToken(result);
		if (!success) {
			sequenceNumber = oldSequenceNumber;
			return false;
		}

		byte *translated1, *translated2;
		byte *strg = result->token;
		bool specialChars = false;
		while (*strg != 0) {
			byte b = *strg;
			if ((b >= 128) || (b == '&') || (b == ';')) {
				specialChars = true;
				break;
			}
			else if ((b >= 'A') && (b <= 'Z'))
				*strg = b + 32;
			strg++;
		}

		// if there are special characters (entity references etc.) in the current
		// token, try to replace them according to the rules defined by the
		// "entityRefs" and "entityVals" arrays
		if (specialChars) {
			translated1 = XMLInputStream::replaceEntityReferences(result->token, tempString);
			translated2 = FilteredInputStream::replaceNonStandardChars(translated1, tempString2, true);

			// copy translated2 into output buffer and split into sub-tokens on-the-fly
			int tokenLen = 0;
			for (int i = 0; translated2[i] != 0; i++) {
				byte b = (byte)translated2[i];
				if ((!isWhiteSpace[b]) && (tokenLen < MAX_TOKEN_LENGTH)) {
					result->token[tokenLen++] = (char)b;
				}
				else if (tokenLen > 0) {
					result->token[tokenLen] = 0;
					result->sequenceNumber = sequenceNumber++;
					addToTermQueue(result);
					tokenLen = 0;
				}
			}
			if (termQueueLength > 0) {
				if (tokenLen > 0) {
					result->token[tokenLen] = 0;
					result->sequenceNumber = sequenceNumber++;
					addToTermQueue(result);
				}
				return getNextToken(result);
			}
		} // end if (specialChars)
		else {
			translated1 = NULL;
			translated2 = NULL;
		}

		result->canBeUsedAsLandmark = true;

		if (translated2 != NULL) {
			int len = strlen((char*)translated2);
			if (len > MAX_TOKEN_LENGTH)
				translated2[MAX_TOKEN_LENGTH] = 0;
			strcpy((char*)result->token, (char*)translated2);
		}

		// special treatment for "-->" (end of comment)
		if (currentlyInComment) {
			bool leaveAsIs = false;
			if ((strcmp((char*)result->token, ">") == 0) && (currentTag[0] == 0)) {
				char prev[3];
				getPreviousChars(prev, 3);
				if ((prev[0] == '-') && (prev[1] == '-') && (prev[2] == '>')) {
					if (xmlCommentMode == COMMENTS_IGNORE)
						strcpy((char*)result->token, "</!-->");
					else {
						InputToken dummy;
						memcpy(&dummy, result, sizeof(InputToken));
						strcpy((char*)dummy.token, "-->");
						addToTermQueue(&dummy);
					}
					currentlyInComment = false;
					leaveAsIs = true;
				}
			}
			if (!leaveAsIs) {
				int in, out;
				switch (xmlCommentMode) {
					case COMMENTS_PLAINTEXT:
						for (in = 0, out = 0; result->token[in] != 0; in++) {
							char c = (char)result->token[in];
							if ((c != '<') && (c != '>'))
								result->token[out++] = (byte)c;
						}
						if (out == 0)
							goto getNextToken_START;
						result->token[out] = 0;
						break;
					case COMMENTS_IGNORE:
						goto getNextToken_START;
						break;
				}
				result->canBeUsedAsLandmark = false;
			}
		} // end if (currentlyInComment)

		// special treatment for closing XML tags: put alternatives into the term queue
		// if necessary
		int len = strlen((char*)result->token);
		assert(len > 0);
		if ((result->token[len - 1] == '>') && (currentTag[0] != 0)) {
			// if this happens, we know that we are inside an XML tag which spans over multiple
			// tokens; add the corresponding attribute-less tag to the term queue
			InputToken dummy;
			memcpy(&dummy, result, sizeof(InputToken));
			if (currentTag[0] == '/') {
				sprintf((char*)dummy.token, "<%s>", currentTag);
				addToTermQueue(&dummy);
			}
			else {
				char prev[2];
				getPreviousChars(prev, 2);
				if (prev[0] == '/') {
					strcpy((char*)result->token, "/>");
					sprintf((char*)dummy.token, "</%s>", currentTag);
					addToTermQueue(&dummy);
				}
			}
			currentTag[0] = 0;
		} // end if ((result->token[len - 1] == '>') && (currentTag[0] != 0))

		result->sequenceNumber = sequenceNumber = oldSequenceNumber;
		sequenceNumber++;
		return true;
	} // end while (true)

	// if the above loop is ever left, this means there are no more tokens
	sequenceNumber = oldSequenceNumber;
	return false;
} // end of getNextToken(InputToken*)


int XMLInputStream::getDocumentType() {
	return DOCUMENT_TYPE_XML;
} // end of getDocumentType()


bool XMLInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	// we are willing to process everything that looks like an XML document
	if (strncmp((char*)fileStart, "<?xml", 5) == 0)
		if ((fileStart[5] == '?') || ((fileStart[5] > 0) && (fileStart[5] <= ' ')))
			return true;

	// if the file does not start with "<?xml", use some stupid heuristics in
	// order to find out whether it is an XML document
	int openCnt = 0, closeCnt = 0;
	for (int i = 0; i < length; i++) {
		if ((i > 64) && (openCnt == 0))
			return false;
		if (fileStart[i] == '<')
			if (fileStart[i + 1] != '<')
				openCnt++;
		if (fileStart[i] == '>')
			if (fileStart[i + 1] != '>')
				closeCnt++;
	}
	if ((openCnt < 4) || (closeCnt < 4))
		return false;
	return true;
} // end of canProcess(char*, byte*, int)



