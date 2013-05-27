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
 * Definition of the class XPathTokenizer.
 *
 * author: Stefan Buettcher
 * created: 2004-12-01
 * changed: 2004-12-01
 **/


#ifndef __QUERY__XPATH_TOKENIZER_H
#define __QUERY__XPATH_TOKENIZER_H


#include "../misc/stringtokenizer.h"


class XPathTokenizer {

private:

	/**
	 * We want to tokenize an XPath expression as easily as possible. The problem
	 * is that we cannot just split whenever we see a '/' because slashes might
	 * appear between quotation marks. So, we first replace all slashes between
	 * quotation marks by a special character before we pass the data to the actual
	 * StringTokenizer. After a call to getNext(), we retransform the string to
	 * its original form. "replacement" tells us what character represents a slash.
	 **/
	char replacement;

	/** The actual tokenizer. **/
	StringTokenizer *tok;

	/** We use this to return data to the caller in getNext(). **/
	char *tempString;

public:

	/** Creates a new instance that tokenizes the string given by "queryString". **/
	XPathTokenizer(const char *queryString);

	/** Destructor. **/
	~XPathTokenizer();

	/** Returns true iff there are more tokens. **/
	bool hasNext();

	/** Returns the next token. Memory must *not* be freed by the caller. **/
	char *getNext();

}; // end of class XPathTokenizer


#endif


