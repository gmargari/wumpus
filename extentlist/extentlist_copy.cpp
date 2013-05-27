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
 * Implementation of the ExtentList_Copy class. An ExtentList_Copy contains
 * a reference to another ExtentList instance that is used to answer the
 * "start biggereq" questions.
 *
 * author: Stefan Buettcher
 * created: 2004-09-27
 * changed: 2005-10-12
 **/


#include "extentlist.h"
#include "../filemanager/securitymanager.h"
#include "../misc/all.h"


ExtentList_Copy::ExtentList_Copy(ExtentList *orig) {
	original = orig;
}


ExtentList_Copy::~ExtentList_Copy() {
}


offset ExtentList_Copy::getLength() {
	return original->getLength();
}


offset ExtentList_Copy::getCount(offset start, offset end) {
	return original->getCount(start, end);
} // end of getCount(offset, offset)


bool ExtentList_Copy::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	return original->getFirstStartBiggerEq(position, start, end);
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Copy::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	return original->getFirstEndBiggerEq(position, start, end);
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Copy::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	return original->getLastStartSmallerEq(position, start, end);
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Copy::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	return original->getLastEndSmallerEq(position, start, end);
} // end of getLastEndSmallerEq(offset, offset*, offset*)


bool ExtentList_Copy::isSecure() {
	return original->isSecure();
}


bool ExtentList_Copy::isAlmostSecure() {
	return original->isAlmostSecure();
}


ExtentList * ExtentList_Copy::makeAlmostSecure(VisibleExtents *restriction) {
	if (isAlmostSecure())
		return this;
	else
		return restriction->restrictList(this);
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_Copy::toString() {
	return original->toString();
}



