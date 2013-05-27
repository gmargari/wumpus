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
 * Definition of class StringBuffer.
 *
 * author: Stefan Buettcher
 * created: 2004-09-07
 * changed: 2004-09-24
 **/


#ifndef __MISC__STRINGBUFFER_H
#define __MISC__STRINGBUFFER_H


#include <sys/types.h>
#include "stringbuffersegment.h"
#include "../filesystem/filesystem.h"
#include "../misc/all.h"


class StringBufferSegment;


class StringBuffer {

	// to make things easier
	friend class StringBufferSegment;

public:

	static const int MAX_STRINGS_PER_SEGMENT = 4096;

	static const int MAX_STRING_SEGMENT_SIZE = 32768;

private:

	/** Where do we store information? **/
	File *file;

	/** Number of StringBufferSegment instances. **/
	int segmentCount;

	/** The segments. **/
	StringBufferSegment **segment;

	/** Number of strings in the StringBuffer. **/
	int stringCount;

	/**
	 * Number of deleteString operations executed. This value is used to trigger
	 * periodic cleanup operations.
	 **/
	int deleteCount;

	/**
	 * An upper bound of the expected length of a string. This value is used for
	 * certain optimizations.
	 **/
	int usualStringLength;

	/** Number of the next segment of which we think that there might be free space. **/
	int nextFreeSegment;

	/** Number of free segments we know of. **/
	int freeSegmentCount;

	/** Numbers of free segments. **/
	int *freeSegments;

	/** Tells us for every segment if it is currently free to take more strings. **/
	bool *segmentIsFree;

public:

	/**
	 * Creates a new StringBuffer instance that reads its data from the file
	 * references by parameter "file". If "file == NULL" or "file" points to an
	 * empty file, a fresh StringBuffer is created. If "file" is non-empty,
	 * "usualStringLength" is ignored. Instead, the value found inside the
	 * file will be used.
	 **/
	StringBuffer(int usualStringLength, File *file);

	/** Deletes the object. **/
	~StringBuffer();

	/**
	 * Saves the StringBuffer's content to the file specified when the object
	 * was constructed.
	 **/
	int saveToFile();

	/** Returns the number of segments. **/
	int getSegmentCount();

	/** Returns the number of strings stored in the StringBuffer. **/
	int getStringCount();

	/**
	 * Adds the string given by "s" to the buffer and returns its index.
	 * This value can later be used for calls to getString.
	 **/
	int addString(char *s);

	/**
	 * Returns the string with index "index". If there is no such string, the
	 * method returns NULL.
	 **/
	char *getString(int index);

	/** Removes the string with index "index" from the buffer. **/
	void deleteString(int index);

private:

	/** Compacts the StringBuffer, i.e. removes unneded space etc. **/
	void compact();
	
}; // end of class StringBuffer


#endif


