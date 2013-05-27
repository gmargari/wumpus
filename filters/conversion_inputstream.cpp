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
 * created: 2007-01-15
 * changed: 2007-01-15
 **/


#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "conversion_inputstream.h"
#include "../index/index.h"
#include "../misc/all.h"


ConversionInputStream::ConversionInputStream(
		const char *fileName, const char *conversionTool) {
	assert(fileName != NULL);
	originalFileName = duplicateString(fileName);
	sprintf(tempFileName, "%s/%s", TEMP_DIRECTORY, "index-conversion-XXXXXXXX.txt");
	randomTempFileName(tempFileName);
	char command[1024];
	sprintf(command, "%s < %s > %s", conversionTool, originalFileName, tempFileName);
	statusCode = MIN(0, system(command));
	if (statusCode != 0) {
		inputFile = -1;
		unlink(tempFileName);
	}
	else {
		inputFile = open(tempFileName, O_RDONLY);
		initialize();
	}
}


ConversionInputStream::~ConversionInputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
	if (originalFileName != NULL) {
		free(originalFileName);
		originalFileName = NULL;
	}
	unlink(tempFileName);
} // end of ~ConversionInputStream()


