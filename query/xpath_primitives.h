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
 * Definition of basic data types used for XPath processing and of the
 * built-in functions of XPath.
 *
 * author: Stefan Buettcher
 * created: 2004-11-30
 * changed: 2008-01-27
 **/


#ifndef __QUERY__XPATH_PRIMITIVES_H
#define __QUERY__XPATH_PRIMITIVES_H


#include "../index/index_types.h"
#include "../index/index.h"


/** Used to represent a single XML element. **/
struct XMLElement {
	offset from;    // start offset
	offset to;      // end offset
	int32_t level;  // nesting level
};


/**
 * This structure is used to manage intermediate and final results of an
 * XPath query. We need to introduce a new data structure because GCL (and thus
 * ExtentList) does not permit overlapping regions.
 **/
struct XMLElementList {
	int length;  // number of elements
	int allocated;  // for how many elements do we have space here?
	XMLElement *elements;
};


/** Different kinds of data types we are dealing with. **/
#define XPATH_TYPE_ERROR -1
#define XPATH_NODESET 1
#define XPATH_STRING 2
#define XPATH_NUMBER 3
#define XPATH_BOOLEAN 4

/** Different kinds of comparison operations. **/
#define XPATH_EQ 4
#define XPATH_NEQ 5
#define XPATH_LTE 6
#define XPATH_GTE 7
#define XPATH_LT 8
#define XPATH_GT 9

/** There are two ways to sort an element list. **/
#define XPATH_DOCUMENT_ORDER 1
#define XPATH_REVERSE_DOCUMENT_ORDER 2

/**
 * Do not allow more than 2 million elements in any XMLElementList, not even
 * for intermediate results. This is so we don't start thrashing.
 **/
#define MAX_XMLELEMENTLIST_LENGTH 2000000


/**
 * In XPath, we have 4 basic data types: Boolean, Double, String and Nodeset.
 * In our implementation, we use one representation for all of them.
 **/
struct XPathData {
	/** Type of this piece of data: node-set, string, boolean or number. **/
	int dataType;
	union {
		bool booleanValue;
		float numberValue;
		char *stringValue;
		XMLElementList *nodeValue;
	};
	/** The Index instance we are using for @get queries. **/
	Index *index;
};


/** Creation and deletion of element lists. **/
XMLElementList * XPath_createEmptyElementList();
XMLElementList * XPath_createElementList(int length, offset *from, offset *to, int32_t *level);
XMLElementList * XPath_duplicateElementList(XMLElementList *list);
void XPath_deleteElementList(XMLElementList *elementList);
void XPath_addToElementList(XMLElementList *list, const XMLElement &toAdd);
void XPath_addToElementList(XMLElementList *list, const XMLElementList &toAdd);

// Sorts a given list, either in XPATH_DOCUMENT_ORDER or in XPATH_REVERSE_DOCUMENT_ORDER.
void XPath_sortElementList(XMLElementList *list, int sortOrder);


/** Frees all memory occupied by the XPathData instance given by "data". **/
XPathData * XPath_createXPathData(Index *index, char *stringValue);
XPathData * XPath_createXPathData(Index *index, float numberValue);
XPathData * XPath_createXPathData(Index *index, bool booleanValue);
void XPath_deleteXPathData(XPathData *data);

/** These functions are used to obtain text from the document. **/
char * XPath_getElement(Index *index, offset from, offset to);
char * XPath_getText(Index *index, offset from, offset to, int level);

/** Miscellaneous functions. **/
char * XPath_extractString(XPathData *argument);
float XPath_extractNumber(XPathData *argument);

/** XPath string functions. **/
XPathData * XPath_string(XPathData *argument);
XPathData * XPath_concat(XPathData **arguments);
XPathData * XPath_starts_with(XPathData *argument1, XPathData *argument2);
XPathData * XPath_contains(XPathData *argument1, XPathData *argument2);
XPathData * XPath_substring_before(XPathData *argument1, XPathData *argument2);
XPathData * XPath_substring_after(XPathData *argument1, XPathData *argument2);
XPathData * XPath_substring(XPathData *argument, int startPosition, int length);
XPathData * XPath_string_length(XPathData *argument);
XPathData * XPath_normalize_space(XPathData *argument);
XPathData * XPath_translate(XPathData *argument, XPathData *from, XPathData *to);

/** XPath boolean functions. **/
XPathData * XPath_boolean(XPathData *argument);
XPathData * XPath_not(XPathData *argument);
XPathData * XPath_true();
XPathData * XPath_false();

/** XPath number functions. **/
XPathData * XPath_number(XPathData *argument);
XPathData * XPath_sum(XPathData **arguments);
XPathData * XPath_floor(XPathData *argument);
XPathData * XPath_ceiling(XPathData *argument);
XPathData * XPath_round(XPathData *argument);
XPathData * XPath_product(XPathData **arguments);
XPathData * XPath_difference(XPathData **arguments);
XPathData * XPath_division(XPathData **arguments);
XPathData * XPath_modulo(XPathData **arguments);

/** XPath comparation functions. **/
XPathData * XPath_compare(XPathData *argument1, XPathData *argument2, int comparison);
bool XPath_compare(XMLElementList *list1, XMLElementList *list2, int comparison);
bool XPath_compare(char *string1, char *string2, int comparison);
bool XPath_compare(bool bool1, bool bool2, int comparison);
bool XPath_compare(float number1, float number2, int comparison);


#endif

