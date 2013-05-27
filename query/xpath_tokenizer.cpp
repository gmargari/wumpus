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
 * Implementation of the class XPathTokenizer.
 *
 * author: Stefan Buettcher
 * created: 2004-12-01
 * changed: 2004-12-01
 **/


#include <string.h>
#include "xpath_tokenizer.h"
#include "../misc/all.h"


XPathTokenizer::XPathTokenizer(const char *queryString) {
	char *qs = duplicateString(queryString);

	// find a character that can safely be used to replace '/'
	replacement = -1;
	for (char c = 1; c < 127; c++)
		if (strchr(qs, c) == NULL) {
			replacement = c;
			break;
		}
	bool insideQuotation = false;
	for (int i = 0; qs[i] != 0; i++) {
		if (qs[i] == '"') {
			if (i == 0)
				insideQuotation = !insideQuotation;
			else if (qs[i - 1] != '\\')
				insideQuotation = !insideQuotation;
		}
		else if (qs[i] == '/')
			if (insideQuotation)
				qs[i] = replacement;
	}

	// create the actual tokenizer and a crazy temp string
	tok = new StringTokenizer(qs, "/");
	free(qs);
	tempString = duplicateString(queryString);
} // end of XPathTokenizer(char*)


XPathTokenizer::~XPathTokenizer() {
	delete tok;
	free(tempString);
} // end of ~XPathTokenizer()


bool XPathTokenizer::hasNext() {
	return tok->hasNext();
}


char * XPathTokenizer::getNext() {
	if (!tok->hasNext())
		return duplicateString("");
	char *result = tok->getNext();
	strcpy(tempString, result);
	for (int i = 0; tempString[i] != 0; i++)
		if (tempString[i] == replacement)
			tempString[i] = '/';
	return tempString;
} // end of getNext()


