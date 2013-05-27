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


#ifndef __FILTERS__CONVERSION_H
#define __FILTERS__CONVERSION_H


#include "inputstream.h"
#include "xml_inputstream.h"


class ConversionInputStream : public XMLInputStream {

protected:

	/** Zero if all the conversion stuff was successful. **/
	int statusCode;

	/** File name of the temporary file. **/
	char tempFileName[1024];

	/** File name of the original input file. **/
	char *originalFileName;

public:

	ConversionInputStream(const char *fileName, const char *conversionTool);

	virtual ~ConversionInputStream();

}; // end of class ConversionInputStream


#endif

