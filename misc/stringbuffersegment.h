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
 * Definition of the StringBufferSegment class.
 *
 * author: Stefan Buettcher
 * created: 2004-09-24
 * changed: 2004-09-24
 **/


#ifndef __MISC__STRINGBUFFERSEGMENT_H
#define __MISC__STRINGBUFFERSEGMENT_H


#include <sys/types.h>
#include "stringbuffer.h"
#include "../filesystem/filesystem.h"
#include "../misc/all.h"


class StringBuffer;


class StringBufferSegment {

	// to make things easier
	friend class StringBuffer;

protected:

	/** This is the maximum length of a StringBuffer segment. **/
	static const int DEFAULT_MAX_SEGMENT_SIZE = 30000;

	/** Maximum number of strings stored in a segment. **/
	static const int DEFAULT_MAX_STRING_COUNT = DEFAULT_MAX_SEGMENT_SIZE / 10;

	/** Where the strings are stored. **/
	char *data;

	/**
	 * Stores for every string at what position it is stored. Empty positions
	 * have value -1.
	 **/
	int16_t *offset;

private:

	/** Maximum number of strings allowed. **/
	int maxStringCount;

	/** Maximum size of the segment, i.e. concatenation of all strings. **/
	int maxLength;

	/**
	 * Tells us the position after which there is guaranteed to be only free
	 * space. We don't worry about fragmentation because the compact() method
	 * deals with that.
	 **/
	int16_t startOfFreeSpace;
	
	/** Number of strings stored in the segment. **/
	int16_t stringCount;

	/**
	 * Number of delete operations performed. This value is used to determine when
	 * to rebuild the segment (compact it). The rebuild threshold is 1/5 * stringCount.
	 * No rebuild is conducted if freeSpaceAfter <= maxSegmentSize / 2.
	 **/
	int16_t deleteCount;

	/** Tells us where in the list of free indexes we have the next free number. **/
	int16_t nextFreeIndex;

	/** Tells us the number of free indexes in the "freeIndex" array. **/
	int16_t freeIndexCount;

	int16_t maxFreeIndexCount;

	/** A list of free indexes, accessed by "addString" and "deleteString". **/
	int16_t *freeIndexes;

public:

	/** Creates a new StringBufferSegment instance with default parameters. **/
	StringBufferSegment();

	/** Creates a new StringBufferSegment instance. **/
	StringBufferSegment(int maxLength, int maxStringCount);

	/** Creates a new StringBufferSegment from data previously written to a File. **/
	StringBufferSegment(File *f);

	/** Deletes the object. **/
	~StringBufferSegment();

	/**
	 * Returns the current length (concatenation of all strings + internal
	 * fragmentation) of the segment.
	 **/
	int getLength();

	/** Returns the maximum length of the segment. **/
	int getMaxLength();

	/** Returns the number of strings stored in the segment. **/
	int getStringCount();

	/** Returns the maximum number of strings stored in the segment. **/
	int getMaxStringCount();

	/**
	 * Adds the string given by "s" to the buffer and returns its index. If the
	 * string cannot be added (space limitations), the method returns -1.
	 **/
	int addString(char *s);

	/**
	 * Returns the string with index "index". If there is no such string, the
	 * method returns NULL.
	 **/
	char *getString(int index);

	/** Removes the string with index "index" from the buffer. **/
	void deleteString(int index);

	/** Compacts the segment, i.e. defragments the storage space. **/
	void compact();

	/** Serializes the object and writes it to the File given by "f". **/
	int saveToFile(File *f);

	/**
	 * Returns true iff the segment can hold an additional string of length
	 * "length" without being compacted first.
	 **/
	bool canAdd(int length);

	/**
	 * Returns the length of the longest string that can be added to the segment
	 * without deleteString(int) or compact() being called. If no string can be
	 * added, the return value is 0.
	 **/
	int maxInsertLength();

private:

	void init(int maxLength, int maxStringCount);

	void computeFreeIndexes();

}; // end of class StringBufferSegment


#endif

