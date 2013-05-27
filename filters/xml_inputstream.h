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
 * This is my implementation of an XML tokenizer. It does not support Unicode,
 * special character encodings, and such. However, it works.
 *
 * author: Stefan Buettcher
 * created: 2004-09-25
 * changed: 2007-11-23
 **/


#ifndef __FILTERS__XML_H
#define __FILTERS__XML_H


#include "inputstream.h"


class XMLInputStream : public FilteredInputStream {

public:

	static const int COMMENTS_DEFAULT = 1;
	static const int COMMENTS_PLAINTEXT = 2;
	static const int COMMENTS_IGNORE = 3;

private:

	byte tempString[MAX_TOKEN_LENGTH * 2];
	byte tempString2[MAX_TOKEN_LENGTH * 2];

	/**
	 * Sometimes, it happens that a single token in the input stream actually
	 * produces several postings in the index. One example is
	 *
	 *   <a href="...">
	 *
	 * where we would like to have "<a" and "<a>" at the same index position. For
	 * this purpose, we keep a queue of InputToken instances. Whenever getNextToken()
	 * called, it first checks whether there is something in the queue. Only if the
	 * queue is empty, the tokenizer proceeds parsing the input stream.
	 **/
	int termQueueLength;
	static const int MAX_QUEUE_LENGTH = 8;
	InputToken termQueue[MAX_QUEUE_LENGTH];

	/** Tells us whether we are at indexing or at query time. **/
	bool atQueryTime;

	/**
	 * If an XML tag spans over multiple tokens (attributes etc.), we have to
	 * keep track of what tag we are currently in so that we can add a closing
	 * tag to the index when we reach the end of the tag. Variable equals the
	 * empty string if not currently inside a container.
	 **/
	char currentTag[MAX_TOKEN_LENGTH * 2];

	/**
	 * Tells us whether we are currently inside an XML comment:
	 *   <!-- This is what a comment looks like. -->
	 **/
	bool currentlyInComment;

	/** How to deal with comments: one of {COMMENTS_DEFAULT, COMMENTS_PLAINTEXT, COMMENTS_IGNORE}. **/
	int xmlCommentMode;

public:

	XMLInputStream();

	XMLInputStream(const char *fileName);

	XMLInputStream(int fd);

	/**
	 * Creates a new instance that reads its input data from the array "inputString".
	 * "atQueryTime" tells us whether the input has to be processed according to indexing
	 * rules or query processing rules.
	 **/
	XMLInputStream(char *inputString, int inputLength, bool atQueryTime);

	virtual ~XMLInputStream();

	static byte *replaceEntityReferences(byte *oldString, byte *newString);

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize();

	/** Puts a copy of the given token into the term queue. **/
	void addToTermQueue(InputToken *token);

}; // end of class XMLInputStream


#endif


