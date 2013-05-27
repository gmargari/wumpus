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
 * The QueryTokenizer class takes a bunch of GCL expressions, separated by
 * commas, and returns them one at a time. We need this class because the
 * StringTokenizer has problems dealing with commas between quotation marks.
 *
 * author: Stefan Buettcher
 * created: 2005-04-14
 * changed: 2005-04-14
 **/


#ifndef __QUERY__QUERYTOKENIZER_H
#define __QUERY__QUERYTOKENIZER_H


class QueryTokenizer {

private:

	char *sequence;

	int inputPosition;

	int inputLength;

public:

	QueryTokenizer(const char *argumentList);

	~QueryTokenizer();

	/**
	 * Returns the pointer to a string containing the next token in the input
	 * sequence. NULL if there are no more tokens. Memory must not be freed or
	 * modified by the caller.
	 **/
	char *getNext();

	/** Returns true iff there are more elements to retrieve. **/
	bool hasNext();

	/**
	 * Returns how many tokens can be extracted from the string in the current
	 * situation.
	 **/
	int getTokenCount();

}; // end of class QueryTokenizer


#endif


