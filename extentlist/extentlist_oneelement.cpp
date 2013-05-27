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


ExtentList_OneElement::ExtentList_OneElement(offset from, offset to) {
	this->from = from;
	this->to = to;
}


ExtentList_OneElement::~ExtentList_OneElement() {
}


offset ExtentList_OneElement::getLength() {
	return 1;
}


bool ExtentList_OneElement::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	if (position <= from) {
		*start = from;
		*end = to;
		return true;
	}
	return false;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_OneElement::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if (position <= to) {
		*start = from;
		*end = to;
		return true;
	}
	return false;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_OneElement::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if (position >= from) {
		*start = from;
		*end = to;
		return true;
	}
	return false;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_OneElement::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	if (position >= to) {
		*start = from;
		*end = to;
		return true;
	}
	return false;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


bool ExtentList_OneElement::isSecure() {
	return false;
}


bool ExtentList_OneElement::isAlmostSecure() {
	return true;
}


ExtentList * ExtentList_OneElement::makeAlmostSecure(VisibleExtents *restriction) {
	return this;
}


char * ExtentList_OneElement::toString() {
	char start[20], end[20];
	char *result = (char*)malloc(40);
	sprintf(result, "[%s, %s]", printOffset(from, start), printOffset(to, end));
	return result;
} // end of toString()


