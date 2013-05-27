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
 * created: 2004-12-04
 * changed: 2004-12-05
 **/


#ifndef __QUERY__XPATH_PREDICATE_H
#define __QUERY__XPATH_PREDICATE_H


#include "xpath_primitives.h"
#include "../index/index.h"


class XPathPredicate {

private:

	Index *index;

	bool syntaxError;

	int type;

	XPathPredicate *subPredicate1;

	XPathPredicate *subPredicate2;

	XPathPredicate *subPredicate3;

	char *functionName;

	float numberValue;

public:

	/** Creates a new predicate from the description found in "string". **/
	XPathPredicate(char *string, Index *index);

	~XPathPredicate();

	/** Returns true iff the predicate is syntactically incorrect. **/
	bool hasSyntaxError();

	/**
	 * Applies the predicate to the element list given by "list" and returns
	 * an array of XPathData objects, telling us the result for every element
	 * in the input list.
	 **/
	XPathData **apply(XMLElementList *list);

	XPathData **applyFunction(XMLElementList *list);

}; // end of class XPathPredicate


#endif


