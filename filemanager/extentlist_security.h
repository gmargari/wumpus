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
 * This is an implementation of the ExtentList interface especially designed
 * for use within the SecurityManager.
 *
 * author: Stefan Buettcher
 * created: 2005-02-20
 * changed: 2005-04-15
 **/


#ifndef __SECURITY_EXTENTLIST_SECURITY_H
#define __SECURITY_EXTENTLIST_SECURITY_H


#include "data_structures.h"
#include "securitymanager.h"
#include "../extentlist/extentlist.h"


class ExtentList_Security : public ExtentList {

private:

	/** Object we got our data from. **/
	VisibleExtents *visible;

	/** The extents we are working with. **/
	VisibleExtent *extents;

	/** Number of extents. **/
	int count;

	/** Last accessed position in the "extents" array. **/
	int currentPosition;

public:

	/**
	 * Creates a new ExtentList_Security instance from the given VisibleExtents
	 * instance. The object will not belong to ExtentList_Security after that!
	 **/
	ExtentList_Security(VisibleExtents *visible);

	~ExtentList_Security();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	virtual offset getTotalSize();

	/** ExtentList_Security is always secure. **/
	virtual bool isSecure();

	/** ExtentList_Security is always almost secure. **/
	virtual bool isAlmostSecure();

	/** Returns this. **/
	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

	virtual int getType();

}; // end of class ExtentList_Security


#endif



