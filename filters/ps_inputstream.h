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
 * The PSInputStream class is responsible for importing PostScript files. PS
 * import is done by converting the PS file to PDF and then using PDFInputStream
 * to deal with the actual data import.
 *
 * author: Stefan Buettcher
 * created: 2004-09-29
 * changed: 2005-10-01
 **/


#ifndef __FILTERS__PS_H
#define __FILTERS__PS_H


#include "inputstream.h"
#include "pdf_inputstream.h"


class PSInputStream : public PDFInputStream {

public:

	static const int MIN_PS_SIZE = 128;

public:

	/**
	 * Creates a new PSInputStream instance converting the given PS file to a
	 * stream of tokens.
	 **/
	PSInputStream(const char *fileName);

	/**
	 * Same as the above constructor, but uses the given cache instance (if non-NULL).
	 * If the file can be found in the cache, the raw text is pulled from the cache.
	 * If not, the conversion is done and the raw text is put into the cache after
	 * the conversion.
	 **/
	PSInputStream(const char *fileName, DocumentCache *cache);

	virtual ~PSInputStream();

	/** Returns DOCUMENT_TYPE_PS. **/
	virtual int getDocumentType();

	/**
	 * Returns true iff this input filter can process the file. Checks file header
	 * for PS signature and insists that the file has a certain minimal size.
	 **/
	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize(const char *fileName, DocumentCache *cache);

}; // end of class PSInputStream


#endif

