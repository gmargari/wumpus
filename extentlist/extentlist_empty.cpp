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
 * created: 2004-11-09
 * changed: 2005-03-07
 **/


#include "extentlist.h"
#include "../misc/all.h"


ExtentList_Empty::ExtentList_Empty() {
}


ExtentList_Empty::~ExtentList_Empty() {
}


offset ExtentList_Empty::getLength() {
	return 0;
}


offset ExtentList_Empty::getCount(offset start, offset end) {
	return 0;
}


bool ExtentList_Empty::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList_Empty::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList_Empty::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList_Empty::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	return false;
}


bool ExtentList_Empty::isSecure() {
	return true;
}


bool ExtentList_Empty::isAlmostSecure() {
	return true;
}


ExtentList * ExtentList_Empty::makeAlmostSecure(VisibleExtents *restriction) {
	return this;
}


char * ExtentList_Empty::toString() {
	return duplicateString("(EMPTY)");
}


int ExtentList_Empty::getType() {
	return TYPE_EXTENTLIST_EMPTY;
}


