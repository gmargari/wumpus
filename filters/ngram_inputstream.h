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
 * This tokenizer is wraps around an arbitrary other tokenizer and splits
 * all tokens into n-grams, where the value of n can be defined via the
 * configuration variable GRAM_SIZE_FOR_NGRAM_TOKENIZER.
 *
 * author: Stefan Buettcher
 * created: 2008-11-02
 * changed: 2008-11-02
 **/


#ifndef __FILTERS__NGRAM_H
#define __FILTERS__NGRAM_H


#include "inputstream.h"
#include "../misc/configurator.h"


class NGramInputStream : public FilteredInputStream {

public:

	enum Ownership {
		TAKE_OWNERSHIP,
		DO_NOT_TAKE_OWNERSHIP
	};

	// Create a new NGramInputStream that is based upon the given input_stream.
	// If ownership == TAKE_OWNERSHIP, take ownership of input_stream and delete
	// it in the destructor.
	NGramInputStream(FilteredInputStream *inputStream, Ownership ownership);

	~NGramInputStream();

	virtual int getFileHandle() {
		return inputStream->getFileHandle();
	}

	virtual void useSmallBuffer() {
		inputStream->useSmallBuffer();
	}

	// Get the next token from the input. If input is exhausted, return false.
	// If the token is an XML tag, return the full tag. Otherwise, split the
	// the token into n-grams and return each n-gram as an individual token.
	virtual bool getNextToken(InputToken *result);

	virtual int getNextCharacter() {
		return inputStream->getNextCharacter();
	}

	virtual void putBackCharacter(byte character) {
		inputStream->putBackCharacter(character);
	}

	virtual void putBackString(byte *string) {
		inputStream->putBackString(string);
	}

	virtual int getDocumentType() {
		return inputStream->getDocumentType();
	}

	virtual void getPreviousChars(char *buffer, int bufferSize) {
		inputStream->getPreviousChars(buffer, bufferSize);
	}

	virtual bool seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber) {
		filePosition = newPosition;
		sequenceNumber = newSequenceNumber;
		return inputStream->seekToFilePosition(newPosition, newSequenceNumber);
	}

	// The default gram size to use, if GRAM_SIZE_FOR_NGRAM_TOKENIZER is not set.
	static const int DEFAULT_GRAM_SIZE = 5;

private:

	// The underlying input stream.
	FilteredInputStream *inputStream;

	// Whether we own inputStream or not.
	Ownership ownership;

	// The n to be used for n-gram indexing.
	int n;

	// A buffer to hold unconsumed characters from the current token or current
	// two tokens.
	char tokenBuffer[2 * MAX_TOKEN_LENGTH + 2];

	// A pointer to the first unconsumed character in tokenBuffer.
	char *posInTokenBuffer;

	// Number of characters remaining in tokenBuffer.
	int charsRemainingInTokenBuffer;

}; // end of class NGramInputStream


#endif

