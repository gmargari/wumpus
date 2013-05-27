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
 * The Simplifier class offers static methods used to simplify GCL operator
 * trees. For example, empty lists are removed from ExtentList_OR instances.
 *
 * author: Stefan Buettcher
 * created: 2005-08-23
 * changed: 2005-11-25
 **/


#ifndef __EXTENTLIST__SIMPLIFIER_H
#define __EXTENTLIST__SIMPLIFIER_H


#include "extentlist.h"


class Simplifier {

public:

	/**
	 * Performs possible simplification operation on the given list, deletes the
	 * list and returns a new, simplified ExtentList instance. Simplifications are
	 * of the type:
	 *
	 *   ExtentList_OR(ExtentList1) => ExtentList1
	 *   ExtentList_OrderedCombination(PostingList1, PostingList2) => PostingList3
	 *
	 * Fairly simple stuff in general.
	 **/
	static ExtentList *simplifyList(ExtentList *list);

	static ExtentList *simplify_AND(ExtentList_AND *list);

	static ExtentList *simplify_Containment(ExtentList_Containment *list);

	static ExtentList *simplify_OR(ExtentList_OR *list);

	static ExtentList *simplify_OrderedCombination(ExtentList_OrderedCombination *list);

	static ExtentList *simplify_Sequence(ExtentList_Sequence *list);

	/**
	 * Returns a new SegmentedPostingList instance that contains all the information
	 * formerly managed by the individual SegmentedPostingList instances given by
	 * "lists". Returns NULL if the lists cannot be combined.
	 **/
	static ExtentList *combineSegmentedPostingLists(ExtentList **lists, int count);

}; // end of class Simplifier


#endif


