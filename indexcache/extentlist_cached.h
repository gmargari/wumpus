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
 * Definition of the ExtentList_Cached class. ExtentList_Cached is used to
 * cache the list of extents corresponding to a complex GCL query, such as
 * "<doc>".."</doc>". This is essential for high-performance query processing.
 *
 * author: Stefan Buettcher
 * created: 2005-01-06
 * changed: 2006-01-18
 **/


#ifndef __INDEX_TOOLS__EXTENTLIST_CACHED_H
#define __INDEX_TOOLS__EXTENTLIST_CACHED_H


#include "indexcache.h"
#include "../index/index_types.h"
#include "../extentlist/extentlist.h"


class IndexCache;


/** This class is used for things like "United States of America". **/
class ExtentList_Cached : public ExtentList {

private:

	/** The IndexCache instance that gave us the data. **/
	IndexCache *cache;

	int cacheID;

	/** Number of extents managed by this instance. **/
	int count;

	/** The Extents we keep track of. **/
	offset *start, *end;

	/** Current pointer in the extents array. **/
	int currentPosition;

	/** For security crap. Most probably useless in this context. **/
	bool almostSecure;

public:

	/**
	 * If "cache" is non-NULL, the object will deregister with the given index
	 * cache upon deletion. If it is NULL, it will instead free the given "start"
	 * and "end" arrays.
	 **/
	ExtentList_Cached(IndexCache *cache, int cacheID, offset *start, offset *end, int count);

	~ExtentList_Cached();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	virtual bool getNth(offset n, offset *start, offset *end);

	virtual bool isAlmostSecure();

	virtual char *toString();

	virtual int getInternalPosition();

	virtual int getType();

	void setAlmostSecure(bool value);

}; // end of class ExtentList_Cached


#endif


