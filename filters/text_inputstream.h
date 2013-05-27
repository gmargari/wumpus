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
 * created: 2004-09-19
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__TEXT_H
#define __FILTERS__TEXT_H


#include "inputstream.h"


class TextInputStream : public FilteredInputStream {

private:

	byte tempString[MAX_TOKEN_LENGTH * 2];

public:

	TextInputStream(const char *fileName);

	TextInputStream(int fd);

	~TextInputStream();

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	virtual bool seekToFilePosition(off_t newPosition, uint32_t newSequenceNumber);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize();

}; // end of class TextInputStream


#endif


