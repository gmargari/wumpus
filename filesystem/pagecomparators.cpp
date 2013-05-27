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
 * created: 2004-10-20
 * changed: 2004-10-21
 **/


#include "pagecomparators.h"


PageIntervalSizeComparator::PageIntervalSizeComparator() {
}


PageIntervalSizeComparator::~PageIntervalSizeComparator() {
}


int PageIntervalSizeComparator::compare(const void *a, const void *b) {
	PageInterval *x = (PageInterval*)a;
	PageInterval *y = (PageInterval*)b;
	if (x->length != y->length)
		return y->length - x->length;
	else
		return x->start - y->start;
} // end of compare(const void*, const void*)


PageIntervalPositionComparator::PageIntervalPositionComparator() {
}


PageIntervalPositionComparator::~PageIntervalPositionComparator() {
}


int PageIntervalPositionComparator::compare(const void *a, const void *b) {
	PageInterval *x = (PageInterval*)a;
	PageInterval *y = (PageInterval*)b;
	if (x->start != y->start)
		return x->start - y->start;
	else
		return y->length - x->length;
} // end of compare(const void*, const void*)



