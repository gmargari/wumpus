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
 * Implementation of the GZIPInputStream class.
 *
 * author: Stefan Buettcher
 * created: 2005-07-22
 * changed: 2005-07-22
 **/


#include "gzip_inputstream.h"
#include "../misc/all.h"


GZIPInputStream::GZIPInputStream(const char *fileName) {
	this->fileName = duplicateString(fileName);
	decompressionCommand = duplicateString("gzip");
	initialize();
}


GZIPInputStream::~GZIPInputStream() {
}


bool GZIPInputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if ((!endsWith(fileName, ".gz")) || (endsWith(fileName, ".tar.gz")))
		return false;
	if (length < 20)
		return false;
	if ((fileStart[0] != 0x1F) || (fileStart[1] != 0x8B))
		return false;
	return true;
} // end of canProcess(char*, byte*, int)



