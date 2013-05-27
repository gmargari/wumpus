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
 * created: 2004-10-08
 * changed: 2010-03-06
 **/


#ifndef __MISC__STRINGTOKENIZER_H
#define __MISC__STRINGTOKENIZER_H

#include <string>
#include <vector>

class StringTokenizer {

public:

	/**
	 * Creates a new StringTokenizer that can be used to split "string" into its
	 * components. The contents of "string" will not be modified. The character
	 * list found in "delim" is used to split up the string.
	 **/
	StringTokenizer(const char *string, const char *delim);

	~StringTokenizer();

	/** Returns true iff there are more tokens in the tokenizer. **/
	bool hasNext();

	/** Returns a pointer to the next token. Do not free this, caller! **/
	char *nextToken();

	/** Same as nextToken(). **/
	char *getNext();

	/** Split the given string into tokens and put them into the vector. **/
	static void split(const std::string &s, const std::string &delim,
	                  std::vector<std::string> *v);

	/**
	 * Reverse operation to tokenize: Glue the strings in the vector together
	 * using delim.
	 **/
	static std::string join(const std::vector<std::string> &v,
	                        const std::string &delim);
	
private:

	char *string, *delim;

	int nextPosition, stringLength;

}; // end of class StringTokenizer


#endif


