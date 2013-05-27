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
 * Implementation of the BZIP2InputStream class.
 *
 * author: Stefan Buettcher
 * created: 2005-07-22
 * changed: 2005-07-22
 **/


#include "bzip2_inputstream.h"
#include "../misc/all.h"


BZIP2InputStream::BZIP2InputStream(const char *fileName) {
	this->fileName = duplicateString(fileName);
	decompressionCommand = duplicateString("bzip2");
	initialize();
}


BZIP2InputStream::~BZIP2InputStream() {
}


bool BZIP2InputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if ((!endsWith(fileName, ".bz2")) || (endsWith(fileName, ".tar.bz2")))
		return false;
	if (length < 20)
		return false;
	if ((fileStart[0] != (byte)'B') || (fileStart[1] != (byte)'Z') || (fileStart[2] != (byte)'h'))
		return false;
	return true;
} // end of canProcess(char*, byte*, int)



