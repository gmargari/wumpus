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
 * The XTeXTInputStream class is responsible for tokenizing MultiText update
 * streams produced as intermediate files by XTeXT. These file consist of a
 * sequence of lines. Each line can start with a "+", a "-", or some other
 * symbol. Only lines starting with "+" or "-" will be indexed.
 *
 * author: Stefan Buettcher
 * created: 2006-10-07
 * changed: 2006-10-07
 **/


#ifndef __FILTERS__XTEXT_H
#define __FILTERS__XTEXT_H


#include "inputstream.h"


class XTeXTInputStream : public FilteredInputStream {

public:

	XTeXTInputStream(const char *fileName);

	XTeXTInputStream(int fd);

	~XTeXTInputStream();

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize();

}; // end of class XTeXTInputStream


#endif


