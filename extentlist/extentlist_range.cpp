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
 * created: 2004-09-24
 * changed: 2005-03-07
 **/


#include "extentlist.h"
#include "../misc/all.h"


ExtentList_Range::ExtentList_Range(offset width, offset maxOffset) {
	this->width = width;
	this->maxOffset = maxOffset;
}


ExtentList_Range::~ExtentList_Range() {
}


offset ExtentList_Range::getLength() {
	if (width <= 0)
		return 0;
	else
		return maxOffset + 1 - width;
} // end of getLength()


offset ExtentList_Range::getCount(offset start, offset end) {
	if (width <= 0)
		return 0;
	offset result = (end - start + 1) - (width - 1);
	if (result <= 0)
		return 0;
	else
		return result;
} // end of getCount(offset, offset)


bool ExtentList_Range::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	if ((position > maxOffset) || (width <= 0))
		return false;
	*start = position;
	*end = position + width - 1;
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Range::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if ((position > maxOffset + width - 1) || (width <= 0))
		return false;
	if (position < width - 1)
		position = width - 1;
	*start = position - width + 1;
	*end = position;
	return true;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Range::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if ((position < 0) || (width <= 0))
		return false;
	*start = position;
	*end = position + width - 1;
	return true;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Range::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	if ((position < width - 1) || (width <= 0))
		return false;
	*start = position - width + 1;
	*end = position;
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


bool ExtentList_Range::isSecure() {
	return false;
}


bool ExtentList_Range::isAlmostSecure() {
	return true;
}


ExtentList * ExtentList_Range::makeAlmostSecure(VisibleExtents *restriction) {
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_Range::toString() {
	char temp[20];
	char *result = (char*)malloc(20);
	sprintf(result, "[%s]", printOffset(width, temp));
	return result;
} // end of toString()


int ExtentList_Range::getType() {
	return TYPE_EXTENTLIST_RANGE;
}



