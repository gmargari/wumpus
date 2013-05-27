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
 * Definition of class PostingList. PostingList extends the ExtentList class and is used
 * to represent a posting list that is associated with a certain term. The implementation
 * can be found in file "postinglist.cpp".
 * Definition of class PostingListUpdateInfo. This class is used to keep track of changes
 * to posting lists caused by changes to the file system indexed.
 *
 * author: Stefan Buettcher
 * created: 2004-09-02
 * changed: 2006-01-18
 **/


#ifndef __INDEX__POSTINGLIST_H
#define __INDEX__POSTINGLIST_H


#include "../extentlist/extentlist.h"


class PostingList;


class PostingList : public ExtentList {

public:

	/** Length of the list (number of postings). **/
	int length;

	/** Size of the data (in bytes). **/
	int size;

	/** Used by the internal iterator. **/
	int currentPosition;

	/** The postings list itself. **/
	offset *postings;

public:

	PostingList();

	/**
	 * Creates a new PostingList instance from offset list given by the parameters
	 * "data" and "length". If "copy" is true, this means that the object cannot just
	 * take the pointer and consider the data its own data from now on but has instead
	 * to allocate own memory and copy the data given. If "alreadySorted" is false,
	 * the object sorts the posting list it has received before it returns.
	 **/
	PostingList(offset *data, int length, bool copy, bool alreadySorted);

	/** Deletes the object, frees all memory. **/
	~PostingList();
	
public:

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);

	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);

	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);

	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	virtual bool getNth(offset n, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	virtual long getMemoryConsumption();

	virtual bool isSecure();

	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

	virtual int getInternalPosition();

	virtual int getType();

}; // end of class PostingList


/**
 * Searches for the first posting in the given array of lenth "n", such that the
 * posting is greater than or equal to the given reference posting. As a local
 * optimization, the binary search starts at the position given by "pos". If no
 * such posting can be found, -1 is returned.
 **/
int findFirstPostingBiggerEq(offset posting, offset *array, int n, int pos);

/**
 * This function works like getFirstPostingBiggerEq, but finds the biggest posting
 * that is smaller than or equal to the given reference posting.
 **/
int findLastPostingSmallerEq(offset posting, offset *array, int n, int pos);


#endif


