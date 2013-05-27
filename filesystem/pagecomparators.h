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
 * The NonFragFileSystem class uses the GeneralAVLTree class to keep track of
 * free page intervals. GeneralAVLTree needs Comparator objects to compare node
 * values. Two Comparator classes are defined here: PageIntervalSizeComparator
 * and PageIntervalPositionComparator.
 *
 * author: Stefan Buettcher
 * created: 2004-10-20
 * changed: 2004-10-21
 **/


#ifndef __FILESYSTEM__PAGECOMPARATORS_H
#define __FILESYSTEM__PAGECOMPARATORS_H


#include "../misc/comparator.h"
#include <sys/types.h>


typedef struct {
	int32_t start;
	int32_t length;
} PageInterval;


class PageIntervalSizeComparator : public Comparator {

public:

	PageIntervalSizeComparator();

	virtual ~PageIntervalSizeComparator();

	virtual int compare(const void *a, const void *b);

};


class PageIntervalPositionComparator : public Comparator {

public:

	PageIntervalPositionComparator();

	virtual ~PageIntervalPositionComparator();

	virtual int compare(const void *a, const void *b);

};


#endif


