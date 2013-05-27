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
 * changed: 2005-08-29
 **/


#ifndef __FILTERS__OFFICE_H
#define __FILTERS__OFFICE_H


#include "inputstream.h"
#include "pdf_inputstream.h"


class OfficeInputStream : public PDFInputStream {

public:

	OfficeInputStream(const char *fileName);

	OfficeInputStream(const char *fileName, DocumentCache *cache);

	~OfficeInputStream();

	virtual int getDocumentType();

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize(const char *fileName, DocumentCache *cache);

}; // end of class OfficeInputStream


#endif

