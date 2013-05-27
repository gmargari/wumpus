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
 * Class definitions of FilteredDocument and FilteredInputStream.
 *
 * author: Stefan Buettcher
 * created: 2004-09-07
 * changed: 2005-08-31
 **/


#ifndef __FILTERS__INPUTFILTER_H
#define __FILTERS__INPUTFILTER_H


#include "../config/config.h"
#include "../index/index_types.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>


class FilteredDocument;
class FilteredInputStream;
class DocumentCache;


typedef struct {

	/** The token itself. **/
	byte token[MAX_TOKEN_LENGTH * 2];

	/**
	 * Where does this token start in the file (byte position). We will need
	 * this information when we reconstruct the original text from an index
	 * range. If filePosition < 0, this means that the token has been inserted
	 * by the filter and is not present in the file.
	 **/
	off_t filePosition;

	/**
	 * 0 if this is the first token in the stream, 1 if it is the second, and
	 * so on. Please note that we can have multiple tokens at the same positions
	 * (schema-independence), which means that we can have the same
	 * sequenceNumber for subsequent tokens.
	 **/
	uint32_t sequenceNumber;

	/** Hash value of the token, as computed by Lexicon::getHashValue. **/
	uint32_t hashValue;

	/** The exact index position described by this InputToken instance. **/
	offset posting;

	/**
	 * Tells us whether this is an artificial token or if it actually appears
	 * in the input file. In the former case, we may not use it to answer @get
	 * queries.
	 **/
	bool canBeUsedAsLandmark;

} InputToken;


class FilteredInputStream {

	friend class FilteredDocument;

public:

	/** Document type ID values for the different document types. **/
	static const int DOCUMENT_TYPE_UNKNOWN = -1;
	static const int DOCUMENT_TYPE_GENERAL = 0;
	static const int DOCUMENT_TYPE_HTML = 1;
	static const int DOCUMENT_TYPE_OFFICE = 2;
	static const int DOCUMENT_TYPE_PDF = 3;
	static const int DOCUMENT_TYPE_PS = 4;
	static const int DOCUMENT_TYPE_TEXT = 5;
	static const int DOCUMENT_TYPE_XML = 6;
	static const int DOCUMENT_TYPE_MBOX = 7;
	static const int DOCUMENT_TYPE_MULTITEXT = 8;
	static const int DOCUMENT_TYPE_MPEG = 9;
	static const int DOCUMENT_TYPE_TREC = 10;
	static const int DOCUMENT_TYPE_TRECMULTI = 11;
	static const int DOCUMENT_TYPE_XTEXT = 12;
	static const int DOCUMENT_TYPE_TROFF = 13;

	/** Largest value in the above list of document types. **/
	static const int MAX_DOCUMENT_TYPE = 13;

	/**
	 * The following are no real document types. They are only used internally
	 * to coordinate file type determination and input stream creation.
	 **/
	static const int DOCUMENT_TYPE_GZIP = 101;
	static const int DOCUMENT_TYPE_BZIP2 = 102;

	/** Document type ID strings. **/
	static const char *DOCUMENT_TYPES[MAX_DOCUMENT_TYPE + 2];

	/** We will not parse documents that are smaller than this. **/
	static const int MINIMUM_LENGTH = 8;

	static const int MAX_FILTERED_RANGE_SIZE = 256 * 1024;

	static const char *TEMP_DIRECTORY;

protected:

	/**
	 * Size of the input buffer in bytes. This value represents the core of
	 * the input buffer, padded with 1024 bytes to both sides, allowing for
	 * lookahead and history.
	 **/
	static const int BUFFER_SIZE = 256 * 1024;

	/** Smaller buffer size that we use in response to a call to useSmallBuffer(). **/
	static const int SMALL_BUFFER_SIZE = 16 * 1024;

	/** Flag used to switch between BUFFER_SIZE and SMALL_BUFFER_SIZE. **/
	bool mustUseSmallBuffer;

	/** File handle of the input file. **/
	int inputFile;

	/** Input buffer. **/
	byte buffer[BUFFER_SIZE + 2048];

	/** Current size of the buffer and read pointer position. **/
	int bufferSize, bufferPos;

	/** Current position in the input file. **/
	off_t filePosition;

	/** Sequence number of the next token. **/
	uint32_t sequenceNumber;

	/** Defines for every character if it is a whitespace character, e.g. ' '. **/
	bool isWhiteSpace[256];

	/** Defines for every character if it always terminates a word, e.g. '>' in XML. **/
	bool isTerminator[256];

	/** Defines for every character if it always starts a new word, e.g. '<' in XML. **/
	bool isInstigator[256];

public:

	/** Empty default constructor. **/
	FilteredInputStream();

	/** Creates a new FilteredInputStream that read data from "inputFile". **/
	FilteredInputStream(const char *inputFile);

	/**
	 * Same as above, but takes a file descriptor instead. The file is closed
	 * by the destructor.
	 **/
	FilteredInputStream(int fd);

	/**
	 * Creates a new FilteredInputStream that read data from an in-memory buffer.
	 * Memory will not be owned by the FilteredInputStream.
	 **/
	FilteredInputStream(char *inputString, int inputLength);

	/** Deletes the object. **/
	virtual ~FilteredInputStream();

	/** Returns the handle to the input file (< 0 in case of error). **/
	virtual int getFileHandle();

	/** Instructs the InputStream to use a smaller read-ahead buffer size. **/
	virtual void useSmallBuffer();

	/**
	 * Returns a FilteredInputStream object that is an instance of one of the
	 * classes that extent FilteredInputStream, e.g. TextInputStream, XMLInputStream ...
	 * It tries to find the appropriate filter for the file. If no filter can be
	 * found, it returns NULL.
	 **/
	static FilteredInputStream *getInputStream(
			const char *inputFile, DocumentCache *cache);

	static FilteredInputStream *getInputStream(
			const char *inputFile, int documentType, DocumentCache *cache);

	/**
	 * Replaces 'Ä' by 'Ae' and so on. If "toLowerCase" is true, it replaces
	 * 'Ä' by 'ae', ...
	 **/
	static byte *replaceNonStandardChars(byte *oldString, byte *newString, bool toLowerCase);

	/** Returns the next token in the document stream or NULL if EOF has been reached. **/
	virtual bool getNextToken(InputToken *result);

	/**
	 * Puts up to "maxCount" tokens into the "result" buffer. Returns the actual
	 * number of tokens read.
	 **/
	virtual int getNextN(InputToken **result, int maxCount);

	/** Returns the next character. Returns -1 if EOF has been reached. **/
	virtual int getNextCharacter();

	/**
	 * If we have read a character that we actually did not want to read, we
	 * can put it back into the buffer.
	 **/
	virtual void putBackCharacter(byte character);

	/** Same as above, but for more than one character. **/
	virtual void putBackString(byte *string);

	/**
	 * Returns the actual string from the file, starting at the position
	 * of the token with sequence number "startToken" and terminating at
	 * the position of the token with sequence number "endToken". The array
	 * "positions" is optional. If it is non-NULL, it contains a non-decreasing
	 * sequence of tokenNumber-filePosition pairs that can be used to speed
	 * up the reconstruction of the text. Memory has to be freed by the caller.
	 **/
	virtual char *getRange(uint32_t startToken, uint32_t endToken,
	                     TokenPositionPair *positions, int *length, int *tokenCount);

	/**
	 * Similar to "getRange", but this method does some preprocessing in
	 * addition, in particular token normalization and removal of spaces.
	 **/
	virtual char *getFilteredRange(uint32_t startToken, uint32_t endToken,
	                     TokenPositionPair *positions, int *length, int *tokenCount);

	/**
	 * Returns the document type ID for the document being processed. One
	 * of the values DOCUMENT_TYPE_HTML, DOCUMENT_TYPE_PDF, ...
	 **/
	virtual int getDocumentType();

	/** Returns the "bufferSize" last characters (before the current symbol). **/
	virtual void getPreviousChars(char *buffer, int bufferSize);

	/**
	 * Allows the caller to skip parts of the input file. Only supported by a
	 * very small number of input filters. Usually returns "false"
	 * (meaning: "not supported").
	 **/
	virtual bool seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber);

	/**
	 * Determines the document type by asking the various input filters if they
	 * can process this file.
	 **/
	static int getDocumentType(const char *fileName, byte *fileStart, int length);

	/**
	 * Returns a string description of the given document type. Memory has to be
	 * freed by the caller.
	 **/
	static char *documentTypeToString(int docType);

	/**
	 * Returns the integer document type ID for the given document type string.
	 * -1 if the type string cannot be found.
	 **/
	static int stringToDocumentType(const char *docTypeString);

protected:

	virtual void initialize();

}; // end of class FilteredInputStream


#endif


