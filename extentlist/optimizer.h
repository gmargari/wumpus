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
 * created: 2005-08-23
 * changed: 2005-08-23
 **/


#ifndef __EXTENTLIST__OPTIMIZER_H
#define __EXTENTLIST__OPTIMIZER_H


#include "extentlist.h"


class Optimizer {

public:
	
	/**
	 * Performs more sophisticated optimization operations on the given list, deletes
	 * the list, and returns a new, simplified ExtentList instance. Optimizations are,
	 * for example, pre-evaluation of ExtentList_OR and ExtentList_AND.
	 **/
	static ExtentList *optimizeList(ExtentList *list);

}; // end of class Optimizer


#endif


