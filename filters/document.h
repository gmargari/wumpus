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
 * created: 2004-09-24
 * changed: 2004-09-25
 **/


#ifndef __FILTERS__DOCUMENT_H
#define __FILTERS__DOCUMENT_H


#include "inputstream.h"


class FilteredDocument {

protected:

	FilteredInputStream *inputStream;

	int tokenCnt;

//	IndexableStringBuffer *buffer;

public:

	FilteredDocument(const char *inputFile);

	~FilteredDocument();

	/** Returns the length of the document, i.e. the number of tokens appearing in it. **/
	virtual int getLength();

	/**
	 * Returns a reference to the token found at position "index" in the document. The
	 * caller need not care about the memory occupied by the string.
	 **/
	virtual char* getToken(int index);

}; // end of class FilteredDocument


#endif


