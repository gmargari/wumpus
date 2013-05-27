/**
 * Copyright (C) 2008 Stefan Buettcher. All rights reserved.
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
 * The implementation of the NGramInputStream class.
 *
 * author: Stefan Buettcher
 * created: 2008-11-02
 * changed: 2008-11-02
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "ngram_inputstream.h"
#include "../index/lexicon.h"
#include "../misc/all.h"


NGramInputStream::NGramInputStream(FilteredInputStream *inputStream, Ownership ownership) {
	this->inputStream = inputStream;
	this->ownership = ownership;
	getConfigurationInt("GRAM_SIZE_FOR_NGRAM_TOKENIZER", &(this->n), DEFAULT_GRAM_SIZE);
	assert(n < MAX_TOKEN_LENGTH);

	// The token buffer is initially empty.
	charsRemainingInTokenBuffer = 0;

	// We start counting tokens at 0.
	sequenceNumber = 0;
}


NGramInputStream::~NGramInputStream() {
	if (ownership == TAKE_OWNERSHIP)
		delete inputStream;
	inputStream = NULL;
}


bool NGramInputStream::getNextToken(InputToken *result) {
getNextToken_START:
	if (charsRemainingInTokenBuffer >= n) {
		memcpy(result->token, posInTokenBuffer, n);
		result->token[n] = 0;
		result->filePosition = filePosition;
		result->sequenceNumber = sequenceNumber++;
		result->canBeUsedAsLandmark = false;

		// Update pointer into token buffer and "chars remaining" counter.
		posInTokenBuffer++;
		charsRemainingInTokenBuffer--;

		return true;
	}
	else {
		// Move the remaining characters in the token buffer to the beginning
		// of the buffer.
		if (posInTokenBuffer != tokenBuffer) {
			memmove(tokenBuffer, posInTokenBuffer, charsRemainingInTokenBuffer);
			posInTokenBuffer = tokenBuffer;
		}

		if (!inputStream->getNextToken(result)) {
			// End of input.
			return false;
		}
		filePosition = result->filePosition;

		if (result->token[0] == '<') {
			// The current token is an XML tag. Report this back as a separate
			// token. Don't split it into n-grams.
			result->sequenceNumber = sequenceNumber++;
			tokenBuffer[0] = '_';
			charsRemainingInTokenBuffer = 1;
			return true;
		}
		else {
			// Get the next token and concatenate it with the remaining characters
			// in tokenBuffer, using '_' as a continuation character.
			int newTokenLen = strlen((char*)result->token);
			assert(newTokenLen > 0);
			assert(charsRemainingInTokenBuffer + newTokenLen <= 2 * MAX_TOKEN_LENGTH);
			memmove(&tokenBuffer[charsRemainingInTokenBuffer], result->token, newTokenLen);
			charsRemainingInTokenBuffer += newTokenLen;			
			tokenBuffer[charsRemainingInTokenBuffer++] = '_';
			goto getNextToken_START;
		}
	}

	assert("We must never get here!" == NULL);
}


