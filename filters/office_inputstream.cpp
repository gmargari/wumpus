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
 * changed: 2009-02-01
 **/


#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "office_inputstream.h"
#include "../indexcache/documentcache.h"
#include "../misc/all.h"


static const char *CONVERSION_TOOL = "abiword";


void OfficeInputStream::initialize(const char *fileName, DocumentCache *cache) {
	PDFInputStream::initialize(NULL, cache);

	if (statusCode != 0) {
		int convTime = currentTimeMillis();

		pid_t child = fork();
		char *oldPath, *newPath;
		switch (child) {
			case -1:
				statusCode = 1;
				break;
			case 0:
#if 0
				oldPath = getenv("PATH");
				if (oldPath == NULL)
					newPath = duplicateString(getenv("HOME"));
				else
					newPath = concatenateStrings(getenv("HOME"), ":", oldPath);
				setenv("PATH", newPath, 1);
				setenv("WUMPUS_CONVERSION_TARGET", tempFileName, 1);
				statusCode = executeCommand(OOFFICE, "-headless", "-p", fileName, NULL, INPUT_CONVERSION_TIMEOUT);
#else
				oldPath = concatenateStrings(tempFileName, ".ps");
				statusCode = executeCommand(
						"abiword", "--disable-crash-dialog", "-p", oldPath, fileName, INPUT_CONVERSION_TIMEOUT);
				if (statusCode == 0)
					statusCode = executeCommand(
							"ps2pdf", oldPath, tempFileName, INPUT_CONVERSION_TIMEOUT);
				if (statusCode == 0)
					statusCode = executeCommand(
							"rm", oldPath, NULL, INPUT_CONVERSION_TIMEOUT);
				free(oldPath);
#endif
				exit(statusCode);
			default:
				waitpid(child, &statusCode, 0);
				break;
		} // end switch (child)

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


OfficeInputStream::OfficeInputStream(const char *fileName) {
	originalFileName = duplicateString(fileName);
	initialize(fileName, NULL);
}


OfficeInputStream::OfficeInputStream(const char *fileName, DocumentCache *cache) {
	originalFileName = duplicateString(fileName);
	initialize(fileName, cache);
} // end of OfficeInputStream(char*, DocumentCache*)


OfficeInputStream::~OfficeInputStream() {
} // end of ~OfficeInputStream()


int OfficeInputStream::getDocumentType() {
	return DOCUMENT_TYPE_OFFICE;
} // end of getDocumentType()


bool OfficeInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < 64)
		return false;
	if (strncmp((char*)fileStart, "PK", 2) == 0) {
		// Open document format (OpenOffice)
		if (endsWith(fileName, ".odt"))
			return true;
		// StarOffice file formats
		if (endsWith(fileName, ".sxw"))
			return true;
	}
	if ((endsWith(fileName, ".doc")) || (endsWith(fileName, ".DOC"))) {
		// Microsoft Office file formats
		if ((fileStart[0] == 0xD0) && (fileStart[1] == 0xCF) && (fileStart[2] == 0x11) &&
		    (fileStart[4] == 0xE0) && (fileStart[5] == 0xA1) && (fileStart[6] == 0xB1))
			return true;		
		if (strncmp((char*)fileStart, "{\\rtf", 5) == 0)
			return true;
	}
	return false;
} // end of canProcess(char*, byte*, int)



