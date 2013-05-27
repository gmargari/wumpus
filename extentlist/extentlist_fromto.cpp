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


#include <stdlib.h>
#include <string.h>
#include "extentlist.h"
#include "../misc/all.h"


ExtentList_FromTo::ExtentList_FromTo(ExtentList *from, ExtentList *to) {
	this->from = from;
	this->to = to;
} // end of ExtentList_FromTo(ExtentList*, ExtentList*)


ExtentList_FromTo::~ExtentList_FromTo() {
	if (from != NULL) {
		delete from;
		from = NULL;
	}
	if (to != NULL) {
		delete to;
		to = NULL;
	}
} // end of ~ExtentList_FromTo()


bool ExtentList_FromTo::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	offset s1, e1, s2, e2, s3, e3;
	if (!from->getFirstStartBiggerEq(position, &s1, &e1))
		return false;
	if (!to->getFirstStartBiggerEq(e1 + 1, &s2, &e2))
		return false;
	if (!from->getLastEndSmallerEq(s2 - 1, &s3, &e3))
		return false;
	*start = s3;
	*end = e2;
	return true;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_FromTo::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	offset s, e;
	if (!ExtentList_FromTo::getLastEndSmallerEq(position - 1, &s, &e))
		s = -1;
	return ExtentList_FromTo::getFirstStartBiggerEq(s + 1, start, end);
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_FromTo::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	offset s, e;
	if (!ExtentList_FromTo::getFirstStartBiggerEq(position + 1, &s, &e))
		e = MAX_OFFSET;
	return ExtentList_FromTo::getLastEndSmallerEq(e - 1, start, end);
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_FromTo::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	offset s1, e1, s2, e2, s3, e3;
	if (!to->getLastEndSmallerEq(position, &s1, &e1))
		return false;
	if (!from->getLastEndSmallerEq(s1 - 1, &s2, &e2))
		return false;
	if (!to->getFirstStartBiggerEq(e2 + 1, &s3, &e3))
		return false;
	*start = s2;
	*end = e3;
	return true;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


offset ExtentList_FromTo::getLength() {
	if (length < 0) {
		offset s, e;
		offset position = 0;
		offset len = 0, size = 0;
		while (ExtentList_FromTo::getFirstStartBiggerEq(position, &s, &e)) {
			position = s + 1;
			size += e - s + 1;
			len++;
		}
		length = len;
		totalSize = size;
	}
	return length;
} // end of getLength()


offset ExtentList_FromTo::getTotalSize() {
	if (totalSize < 0) {
		offset s, e;
		offset position = 0;
		offset len = 0, size = 0;
		while (ExtentList_FromTo::getFirstStartBiggerEq(position, &s, &e)) {
			position = s + 1;
			size += e - s + 1;
			len++;
		}
		length = len;
		totalSize = size;
	}
	return totalSize;
} // end of getTotalSize()


long ExtentList_FromTo::getMemoryConsumption() {
	long result = 0;
	result += from->getMemoryConsumption();
	result += to->getMemoryConsumption();
	return result;
}


void ExtentList_FromTo::optimize() {
	from->optimize();
	to->optimize();
} // end of optimize()


bool ExtentList_FromTo::isSecure() {
	return false;
} // end of isSecure()


bool ExtentList_FromTo::isAlmostSecure() {
	if (!from->isAlmostSecure())
		return false;
	if (!to->isAlmostSecure())
		return false;
	return true;
} // end of isAlmostSecure()


ExtentList * ExtentList_FromTo::makeAlmostSecure(VisibleExtents *restriction) {
	if (!from->isAlmostSecure())
		from = from->makeAlmostSecure(restriction);
	if (!to->isAlmostSecure())
		to = to->makeAlmostSecure(restriction);
	return this;
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_FromTo::toString() {
	char *fromString = from->toString();
	char *toString = to->toString();
	char *result = (char*)malloc(strlen(fromString) + strlen(toString) + 8);
	sprintf(result, "(%s .. %s)", fromString, toString);
	free(fromString);
	free(toString);
	return result;
} // end of toString()


int ExtentList_FromTo::getType() {
	return TYPE_EXTENTLIST_FROMTO;
}


void ExtentList_FromTo::detachSubLists() {
	from = NULL;
	to = NULL;
} // end of detachSubLists()


