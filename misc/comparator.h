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
 * Definition of the Comparator interface. Comparator is used to perform abstract
 * value comparation, for instance inside the GeneralAVLTree implementation.
 *
 * author: Stefan Buettcher
 * created: 2004-10-19
 * changed: 2004-10-19
 **/


#ifndef __MISC__COMPARATOR_H
#define __MISC__COMPARATOR_H


class Comparator {

public:

	virtual ~Comparator() = 0;

	/**
	 * Compares the object referenced by "a" and "b". Returns -1 if a < b,
	 * +1 if a > b, and 0 if a == b.
	 **/
	virtual int compare(const void *a, const void *b) = 0;

}; // end of class Comparator


#endif


