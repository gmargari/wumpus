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
 * author: Stefan Buettcher
 * created: 2004-09-29
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__PDF_H
#define __FILTERS__PDF_H


#include "inputstream.h"


class PDFInputStream : public FilteredInputStream {

	friend class PSInputStream;

public:

	static const int MIN_PDF_SIZE = 128;

protected:

	/** Zero if all the conversion stuff was successful. **/
	int statusCode;

	/** File name of the temporary file. **/
	char tempFileName[1024];

	/** File name of the original input file. **/
	char *originalFileName;

	byte tempString[MAX_TOKEN_LENGTH * 2];

	bool closingDocWasThere;

public:

	PDFInputStream();

	PDFInputStream(const char *fileName);

	PDFInputStream(const char *fileName, DocumentCache *cache);

	virtual ~PDFInputStream();

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize(const char *fileName, DocumentCache *cache);

	bool getTextFromCache(DocumentCache *cache, const char *tempFileName);

}; // end of class PDFInputStream


#endif

