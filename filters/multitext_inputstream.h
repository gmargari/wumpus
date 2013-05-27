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
 * The MultiTextInputStream class is used to read an input file that has been
 * produced by a MultiText-compatible filtering tool. The input looks like:
 * > batch
 * > term1
 * > 1
 * > term2
 * > 2
 * > term3
 * > 3
 * multiple terms allowed to be indexed at the same position. Filtering and case
 * folding should not be performed by the filtering process because they are
 * automatically done by the indexing system.
 *
 * We also allow an extension of the MultiText filter scheme in which each
 * index position can be accompanied by a probability value.
 * 
 * author: Stefan Buettcher
 * created: 2004-11-05
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__MULTITEXT_H
#define __FILTERS__MULTITEXT_H


#include "inputstream.h"


class MultiTextInputStream : public FilteredInputStream {

private:

	byte tempString[MAX_TOKEN_LENGTH * 2];

	double lastProbabilitySeen;

public:

	MultiTextInputStream(const char *fileName);

	MultiTextInputStream(int fd);

	~MultiTextInputStream();

	virtual int getDocumentType();

	virtual bool getNextToken(InputToken *result);

	static bool canProcess(const char *fileName, byte *fileStart, int length);

	double getLastProbabilitySeen();

protected:

	void initialize();

}; // end of class MultiTextInputStream


#endif


