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
 * Implementation of the input stream for HTML input.
 *
 * author: Stefan Buettcher
 * created: 2004-09-29
 * changed: 2005-11-30
 **/


#include "html_inputstream.h"
#include "text_inputstream.h"
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "../misc/alloc.h"


HTMLInputStream::HTMLInputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of HTMLInputStream(char*)


HTMLInputStream::HTMLInputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of HTMLInputStream(int)


HTMLInputStream::~HTMLInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
} // end of ~HTMLInputStream()


bool HTMLInputStream::getNextToken(InputToken *result) {
	bool success = XMLInputStream::getNextToken(result);
	return success;
} // end of getNextToken(InputToken*)


int HTMLInputStream::getDocumentType() {
	return DOCUMENT_TYPE_HTML;
} // end of getDocumentType()


bool HTMLInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (!TextInputStream::canProcess(fileName, fileStart, length))
		return false;
	char *fs = (char*)malloc(length + 4);
	strcpy(fs, (char*)fileStart);
	for (int i = 0; fs[i] != 0; i++)
		if ((fs[i] >= 'A') && (fs[i] <= 'Z'))
			fs[i] = (char)('a' + (fs[i] - 'A'));
	char *html = strstr(fs, "<html");
	if (html == NULL) {
		free(fs);
		return false;
	}
	if (((long)html - (long)fs) > 512) {
		free(fs);
		return false;
	}
	if (strstr(fs, "<!doctype html") != NULL) {
		free(fs);
		return true;
	} 
	if (strstr(fs, "<head") != NULL) {
		free(fs);
		return true;
	}
	if (strstr(fs, "<body") != NULL) {
		free(fs);
		return true;
	}
	if ((strstr(fs, "<title>") != NULL) &&
	    (strstr(fs, "</title>") != NULL)) {
		free(fs);
		return true;
	}
	free(fs);
	return false;
} // end of canProcess(char*, byte*, int)



