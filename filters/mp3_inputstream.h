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
 * Definition of the MP3InputStream class. MP3InputStream reads IDv3-type tag
 * information from music files. MP3InputStream uses XMLInputStream as an
 * underlying service to get the information found in the audio file into the
 * right format.
 *
 * author: Stefan Buettcher
 * created: 2005-03-19
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__MP3_H
#define __FILTERS__MP3_H


#include "inputstream.h"
#include "xml_inputstream.h"


class MP3InputStream : public XMLInputStream {

public:

	static const int ID3_TAG_SIZE = 128;

private:

	char tempFileName[1024];

	byte tempString[MAX_TOKEN_LENGTH * 2];

public:

	MP3InputStream(const char *fileName);

	MP3InputStream(int fd);

	virtual ~MP3InputStream();

	virtual int getDocumentType();

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize();

}; // end of class MP3InputStream


#endif



