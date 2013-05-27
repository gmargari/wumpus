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
 * Implementation of the class FilteredDocument.
 *
 * author: Stefan Buettcher
 * created: 2004-09-19
 * changed: 2004-09-29
 **/


#include "document.h"
#include "inputstream.h"
#include "../misc/all.h"


FilteredDocument::FilteredDocument(const char *inputFile) {
	char token[MAX_TOKEN_LENGTH + 4];
	inputStream = new FilteredInputStream(inputFile);
	tokenCnt = 0;
//	buffer = new IndexableStringBuffer();
//	while (inputStream->getNextToken(token) != NULL)
//		buffer->addString(token);
	delete inputStream;
} // end of FilteredDocument(char*)
                                                                                                
                                                                                                
FilteredDocument::~FilteredDocument() {
//	delete buffer;
} // end of ~FilteredDocument()
                                                                                                
                                                                                                
int FilteredDocument::getLength() {
//	return buffer->getStringCount();
} // end of getLength()


char * FilteredDocument::getToken(int index) {
//	return buffer->getString(index);
} // end of getToken(offset)


