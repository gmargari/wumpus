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
 * changed: 2006-04-28
 **/


#include <string.h>
#include "extentlist.h"
#include "../misc/all.h"


ExtentList_Containment::ExtentList_Containment(
		ExtentList *container, ExtentList *containee, bool returnContainer, bool inverted) {
	this->container = container;
	this->containee = containee;
	this->returnContainer = returnContainer;
	this->inverted = inverted;
	lastContainerStart = MAX_OFFSET;
	lastContainerEnd = -1;
} // end of ExtentList_Containment(...)


ExtentList_Containment::~ExtentList_Containment() {
	if (container != NULL) {
		delete container;
		container = NULL;
	}
	if (containee != NULL) {
		delete containee;
		containee = NULL;
	}
} // end of ~ExtentList_Containment()


offset ExtentList_Containment::getLength() {
	if (length >= 0)
		return length;

	offset s, e;
	offset position = 0;
	offset result = 0;
	if (returnContainer) {
		offset size = 0;
		while (ExtentList_Containment::getFirstStartBiggerEq(position, &s, &e)) {
			position = s + 1;
			size += e - s + 1;
			result++;
		}
		totalSize = size;
		return (length = result);
	}
	else {
		if (inverted) {
			while (true) {
				offset s1, e1, s2, e2;
				if (!containee->getFirstStartBiggerEq(position, &s1, &e1))
					return result;
				if (!container->getFirstEndBiggerEq(e1, &s2, &e2))
					s2 = s1 + 1;
				if (s2 > s1) {
					result++;
					position = s1 + 1;
				}
				else {
					if (!containee->getFirstEndBiggerEq(e2 + 1, &s1, &e1))
						return result;
					position = s1;
				}
			}
		} // end (inverted)
		else {
			offset s1, e1, s2, e2;
			if (!containee->getFirstStartBiggerEq(position, &s1, &e1))
				return result;
			while (true) {
				if (!container->getFirstEndBiggerEq(e1, &s2, &e2))
					return result;
				if (s2 <= s1) {
					result += containee->getCount(s1, e2);
					if (!containee->getFirstEndBiggerEq(e2 + 1, &s1, &e1))
						return result;
				}
				else if (!containee->getFirstStartBiggerEq(s2, &s1, &e1))
					return result;
			}
			return result;
		} // end (!inverted)
	}
	assert("We should never get here!" == NULL);
} // end of getLength()


offset ExtentList_Containment::getCount(offset start, offset end) {
	offset result = 0;
	if ((returnContainer) || (inverted)) {
		offset s, e;
		offset position = start;
		while (ExtentList_Containment::getFirstStartBiggerEq(position, &s, &e)) {
			if (e > end)
				return result;
			result++;
			position = s + 1;
		}
		return result;
	}
	else {
		offset s1, e1, s2, e2;
		if (!containee->getFirstStartBiggerEq(start, &s1, &e1))
			return result;
		while (e1 <= end) {
			if (!container->getFirstEndBiggerEq(e1, &s2, &e2))
				return result;
			if (s2 <= s1) {
				if (e2 <= end)
					result += containee->getCount(s1, e2);
				else
					result += containee->getCount(s1, end);
				if (!containee->getFirstEndBiggerEq(e2 + 1, &s1, &e1))
					return result;
			}
			else if (!containee->getFirstStartBiggerEq(s2, &s1, &e1))
				return result;
		}
		return result;
	}
} // end of getCount(offset, offset)


bool ExtentList_Containment::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	if (returnContainer) {
		offset s, e;
		if (!container->getFirstStartBiggerEq(position, &s, &e))
			return false;
		return ExtentList_Containment::getFirstEndBiggerEq(e, start, end);
	}
	else {
		if (inverted) {
			while (true) {
				offset s1, e1, s2, e2;
				if (!containee->getFirstStartBiggerEq(position, &s1, &e1))
					return false;
				if (!container->getFirstEndBiggerEq(e1, &s2, &e2))
					s2 = s1 + 1;
				if (s2 > s1) {
					*start = s1;
					*end = e1;
					return true;
				}
				if (!containee->getFirstEndBiggerEq(e2 + 1, &s1, &e1))
					return false;
				position = s1;
			}
		} // end (inverted)
		else {
			while (true) {
				offset s1, e1, s2, e2;
				if (!containee->getFirstStartBiggerEq(position, &s1, &e1))
					return false;
				if ((s1 >= lastContainerStart) && (e1 <= lastContainerEnd)) {
					*start = s1;
					*end = e1;
					return true;
				}
				else {
					if (!container->getFirstEndBiggerEq(e1, &s2, &e2))
						return false;
					assert(e2 >= e1);
					lastContainerStart = s2;
					lastContainerEnd = e2;
					if (s2 <= s1) {
						*start = s1;
						*end = e1;
						return true;
					}
				}
				position = s2;
			}
		} // end (!inverted)
	}
	return false;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Containment::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	if (returnContainer) {
		if (inverted) {
			while (true) {
				offset s1, e1, s2, e2;
				if (!container->getFirstEndBiggerEq(position, &s1, &e1))
					return false;
				if (!containee->getFirstStartBiggerEq(s1, &s2, &e2))
					e2 = e1 + 1;
				if (e2 > e1) {
					*start = s1;
					*end = e1;
					return true;
				}
				if (!container->getFirstStartBiggerEq(s2 + 1, &s1, &e1))
					return false;
				position = e1;
			}
		} // end (inverted)
		else {
			while (true) {
				offset s1, e1, s2, e2;
				if (!container->getFirstEndBiggerEq(position, &s1, &e1))
					return false;
				if (!containee->getFirstStartBiggerEq(s1, &s2, &e2))
					return false;
				if (e2 <= e1) {
					*start = s1;
					*end = e1;
					return true;
				}
				position = e2;
			}
		} // end (!inverted)
	}
	else {
		offset s, e;
		if (!containee->getFirstEndBiggerEq(position, &s, &e))
			return false;
		if ((s >= lastContainerStart) && (e <= lastContainerEnd)) {
			*start = s;
			*end = e;
			return true;
		}
		else
			return ExtentList_Containment::getFirstStartBiggerEq(s, start, end);
	}
	return false;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Containment::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	if (returnContainer) {
		if (inverted) {
			while (true) {
				offset s1, e1, s2, e2;
				if (!container->getLastStartSmallerEq(position, &s1, &e1))
					return false;
				if (!containee->getLastEndSmallerEq(e1, &s2, &e2))
					s2 = s1 - 1;
				if (s2 < s1) {
					*start = s1;
					*end = e1;
					return true;
				}
				if (!container->getLastEndSmallerEq(e2 - 1, &s1, &e1))
					return false;
				position = s1;
			}
		} // end (inverted)
		else {
			while (true) {
				offset s1, e1, s2, e2;
				if (!container->getLastStartSmallerEq(position, &s1, &e1))
					return false;
				if (!containee->getLastEndSmallerEq(e1, &s2, &e2))
					return false;
				if (s2 >= s1) {
					*start = s1;
					*end = e1;
					return true;
				}
				position = s2;
			}
		} // end (!inverted)
	}
	else {
		offset s, e;
		if (!containee->getLastStartSmallerEq(position, &s, &e))
			return false;
		return ExtentList_Containment::getLastEndSmallerEq(e, start, end);
	}
	return false;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Containment::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	if (returnContainer) {
		offset s, e;
		if (!container->getLastEndSmallerEq(position, &s, &e))
			return false;
		return ExtentList_Containment::getLastStartSmallerEq(s, start, end);
	}
	else {
		if (inverted) {
			while (true) {
				offset s1, e1, s2, e2;
				if (!containee->getLastEndSmallerEq(position, &s1, &e1))
					return false;
				if (!container->getLastStartSmallerEq(s1, &s2, &e2))
					e2 = e1 - 1;
				if (e2 < e1) {
					*start = s1;
					*end = e1;
					return true;
				}
				if (!containee->getLastStartSmallerEq(s2 - 1, &s1, &e1))
					return false;
				position = e1;
			}
		} // end (inverted)
		else {
			while (true) {
				offset s1, e1, s2, e2;
				if (!containee->getLastEndSmallerEq(position, &s1, &e1))
					return false;
				if (!container->getLastStartSmallerEq(s1, &s2, &e2))
					return false;
				if (e2 >= e1) {
					*start = s1;
					*end = e1;
					return true;
				}
				position = e2;
			}
		} // end (!inverted)
	}
	return false;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


int ExtentList_Containment::getNextN(offset from, offset to, int n, offset *start, offset *end) {
	int result = 0;
	while (result < n) {
		if (!ExtentList_Containment::getFirstStartBiggerEq(from, &start[result], &end[result]))
			break;
		if (*end > to)
			break;
		from = start[result] + 1;
		result++;
	}
	return result;
} // end of getNextN(offset, offset, int, offset*, offset*)


long ExtentList_Containment::getMemoryConsumption() {
	long result = 0;
	result += container->getMemoryConsumption();
	result += containee->getMemoryConsumption();
	return result;
} // end of getMemoryConsumption()


void ExtentList_Containment::optimize() {
	container->optimize();
	containee->optimize();
}


int ExtentList_Containment::getType() {
	return TYPE_EXTENTLIST_CONTAINMENT;
}


bool ExtentList_Containment::isSecure() {
	if (returnContainer)
		return (containee->isAlmostSecure() && container->isSecure());
	else if (inverted)
		return (containee->isSecure() && container->isSecure());
	else
		return (containee->isAlmostSecure() && container->isSecure());
} // end of isSecure()


bool ExtentList_Containment::isAlmostSecure() {
	if (returnContainer)
		return (containee->isAlmostSecure() && container->isAlmostSecure());
	else
		return (containee->isAlmostSecure() && container->isSecure());
} // end of isAlmostSecure()


ExtentList * ExtentList_Containment::makeAlmostSecure(VisibleExtents *restriction) {
	if (returnContainer) {
		if (!containee->isAlmostSecure())
			containee = containee->makeAlmostSecure(restriction);
		if (!container->isAlmostSecure())
			container = container->makeAlmostSecure(restriction);
		return this;
	}
	else {
		if (!containee->isAlmostSecure())
			containee = containee->makeAlmostSecure(restriction);
		if (!container->isSecure())
			container = container->makeSecure(restriction);
		return this;
	}
} // end of makeAlmostSecure(VisibleExtents*)


char * ExtentList_Containment::toString() {
	char *containerString = container->toString();
	char *containeeString = containee->toString();
	char *result = (char*)malloc(strlen(containerString) + strlen(containeeString) + 8);
	sprintf(result, "(%s %s%c %s)",
			(returnContainer ? containerString : containeeString),
			(inverted ? "/" : ""),
			(returnContainer ? '>' : '<'),
			(returnContainer ? containeeString : containerString));
	free(containerString);
	free(containeeString);
	return result;
} // end of toString()


void ExtentList_Containment::detachSubLists() {
	container = NULL;
	containee = NULL;
} // end of detachSubLists()


