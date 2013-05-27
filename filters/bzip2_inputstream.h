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
 * The BZIP2InputStream class is used to import data that comes in compressed
 * form (BZIP2). The document type reported to the indexing system depends on
 * the data found inside the .bz2 file.
 *
 * author: Stefan Buettcher
 * created: 2005-07-22
 * changed: 2005-07-22
 **/


#ifndef __FILTERS__BZIP2_H
#define __FILTERS__BZIP2_H


#include "compressed_inputstream.h"


class BZIP2InputStream : public CompressedInputStream {

public:

	/** Creates a new BZIP2InputStream from the given file. **/
	BZIP2InputStream(const char *fileName);

	virtual ~BZIP2InputStream();

	/**
	 * Returns true iff the file name ends with ".bz2" and the file starts with
	 * the usual BZIP2 header information.
	 **/
	static bool canProcess(const char *fileName, byte *fileStart, int length);

}; // end of class BZIP2InputStream


#endif


