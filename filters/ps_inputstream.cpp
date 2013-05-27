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
 * Implementation of the PSInputStream class.
 *
 * author: Stefan Buettcher
 * created: 2004-09-29
 * changed: 2005-10-01
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "ps_inputstream.h"
#include "../indexcache/documentcache.h"
#include "../misc/all.h"


static const char *PS2PDF = "ps2pdf";


void PSInputStream::initialize(const char *fileName, DocumentCache *cache) {
	PDFInputStream::initialize(NULL, cache);

	if (statusCode != 0) {
		int convTime = currentTimeMillis();
		statusCode = executeCommand((char*)PS2PDF, fileName, tempFileName,
				INPUT_CONVERSION_TIMEOUT);
		if (statusCode == 0) {
			char *tempFile = duplicateString(tempFileName);
			PDFInputStream::initialize(tempFile, NULL);
			unlink(tempFile);
			free(tempFile);
			if ((statusCode == 0) && (cache != NULL)) {
				convTime = currentTimeMillis() - convTime;
				if (convTime < 0)
					convTime += 24 * 3600 * 1000;
				cache->addDocumentTextFromFile(originalFileName, tempFileName, convTime);
			}
		}
	} // end if (statusCode != 0)

	if (statusCode != 0) {
		inputFile = -1;
		unlink(tempFileName);
	}
} // end of initialize(char*, DocumentCache*)


PSInputStream::PSInputStream(const char *fileName) {
	originalFileName = duplicateString(fileName);
	PSInputStream::initialize(fileName, NULL);
}


PSInputStream::PSInputStream(const char *fileName, DocumentCache *cache) {
	originalFileName = duplicateString(fileName);
	PSInputStream::initialize(fileName, cache);
}


PSInputStream::~PSInputStream() {
} // end of ~PSInputStream()


int PSInputStream::getDocumentType() {
	return DOCUMENT_TYPE_PS;
} // end of getDocumentType()


bool PSInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < MIN_PS_SIZE)
		return false;
	if (strncasecmp((char*)fileStart, "%!PS-Adobe-", 11) == 0)
		return true;
	return false;
} // end of canProcess(char*, byte*, int)



