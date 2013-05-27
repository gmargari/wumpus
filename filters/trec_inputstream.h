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
 * Special tokenizer for reading TREC-formatted input files. TREC files come
 * in an XML-like format, but our XML parser is too slow to parse large input
 * files efficiently.
 *
 * author: Stefan Buettcher
 * created: 2005-03-25
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__TREC_H
#define __FILTERS__TREC_H


#include "inputstream.h"


class TRECInputStream : public FilteredInputStream {

private:

	/**
	 * Every input character goes through this table and is transformed to something
	 * else. Uppercase characters, for instance, are transformed to their lowercase
	 * pendants.
	 **/
	byte translationTable[256];

	/** Tell us for every possible character whether it is a numerical char. **/
	byte isNum[256];

public:

	TRECInputStream() { }

	TRECInputStream(const char *fileName);

	/**
	 * Creates a new TRECInputStream for the data source given by "fd". The
	 * file descriptor is closed by the destructor.
	 **/
	TRECInputStream(int fd);

	virtual ~TRECInputStream();

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	virtual bool seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

	bool reload(int *bufPos, int *bufSize);

protected:

	void initialize();

	off_t bufferStartInFile;

}; // end of class TRECInputStream


#endif


