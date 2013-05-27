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
 * This input filter can process e-mails that are stored in the "mboxo" or
 * "mboxrd" file format.
 *
 * author: Stefan Buettcher
 * created: 2004-11-10
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__MBOX_H
#define __FILTERS__MBOX_H


#include "inputstream.h"
#include <stdio.h>


class MBoxInputStream : public FilteredInputStream {

private:

	byte tempString[MAX_TOKEN_LENGTH * 2];

	/** Do not process files that are smaller than this. **/
	static const int MIN_MBOX_LENGTH = 128;

	FILE *inputStream;

	bool closingDocAlreadyThere;

	/**
	 * When we encounter a "From - " line, we return a "</doc>" immediately.
	 * The corresponding "<doc>" is returned by the next call to getNextToken.
	 * We used this variable to pass the necessary information between two
	 * subsequent calls to the method.
	 **/
	bool nextCallMustReturnDocTag;

public:

	MBoxInputStream(const char *fileName);

	MBoxInputStream(int fd);

	~MBoxInputStream();

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

protected:

	void initialize();

}; // end of class MBoxInputStream


#endif


