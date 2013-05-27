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
 * Implementation of the XPath basic functions.
 *
 * author: Stefan Buettcher
 * created: 2004-11-30
 * changed: 2008-01-27
 **/


#include <math.h>
#include <stdio.h>
#include <string.h>
#include "xpath_primitives.h"
#include "getquery.h"
#include "../misc/all.h"
#include "../filters/inputstream.h"


#define XPATH_EPSILON (1.0E-4)


XMLElementList * XPath_createElementList(int length, offset *from, offset *to, int32_t *level) {
	length = MIN(length, MAX_XMLELEMENTLIST_LENGTH);

	XMLElementList *result = typed_malloc(XMLElementList, 1);
	result->length = length;
	result->allocated = MAX(16, (3 * length) / 2);
	result->elements = typed_malloc(XMLElement, result->allocated);
	for (int i = 0; i < length; i++) {
		result->elements[i].from = from[i];
		result->elements[i].to = to[i];
		result->elements[i].level = level[i];
	}
	if (from != NULL)
		free(from);
	if (to != NULL)
		free(to);
	if (level != NULL)
		free(level);
	return result;
} // end of XPath_createElementList(int, offset*, offset*)


XMLElementList * XPath_createEmptyElementList() {
	return XPath_createElementList(0, NULL, NULL, NULL);
} // end of XPath_createEmptyElementList()


XMLElementList * XPath_duplicateElementList(const XMLElementList &list) {
	XMLElementList *result = typed_malloc(XMLElementList, 1);
	result->length = list.length;
	result->allocated = MAX(1, list.length);
	result->elements = typed_malloc(XMLElement, result->allocated);
	memcpy(result->elements, list.elements, list.length * sizeof(XMLElement));
	return result;
} // end of XPath_duplicateElementList(const XMLElementList&)


void XPath_deleteElementList(XMLElementList *list) {
	free(list->elements);
	free(list);
} // end of XPath_deleteElementList(XMLElementList*)


void XPath_addToElementList(XMLElementList *list, const XMLElement &toAdd) {
	if (list->length >= MAX_XMLELEMENTLIST_LENGTH)
		return;
	if (list->length + 1 > list->allocated) {
		// expand array
		list->allocated = MAX(list->allocated + 16, (3 * list->allocated) / 2);
		typed_realloc(XMLElement, list->elements, list->allocated);
	}
	list->elements[list->length++] = toAdd;
} // end of XPath_addToElementList(XMLElementList*, const XMLElement&)


void XPath_addToElementList(XMLElementList *list, const XMLElementList &toAdd) {
	int numToAdd = MIN(toAdd.length, MAX_XMLELEMENTLIST_LENGTH - list->length);
	if (list->length + numToAdd > list->allocated) {
		// expand array
		list->allocated = (3 * (list->length + numToAdd)) / 2;
    typed_realloc(XMLElement, list->elements, list->allocated);
	}
	memcpy(&list->elements[list->length], toAdd.elements, numToAdd * sizeof(XMLElement));
	list->length += numToAdd;
} // end of end of XPath_addToElementList(XMLElementList*, const XMLElementList&)


static int compareByDocumentOrder(const void *a, const void *b) {
	XMLElement *x = (XMLElement*)a;
	XMLElement *y = (XMLElement*)b;
	offset diff = x->from - y->from;
	if (diff < 0)
		return -1;
	else if (diff > 0)
		return +1;
	else
		return 0;
} // end of compareByDocumentOrder(...)


void XPath_sortElementList(XMLElementList *list, int sortOrder) {
	assert((sortOrder == XPATH_DOCUMENT_ORDER) || (sortOrder == XPATH_REVERSE_DOCUMENT_ORDER));
	if (list->length <= 1)
		return;

	// sort
	qsort(list->elements, list->length, sizeof(XMLElement), compareByDocumentOrder);

	// remove duplicates
	int outPos = 1;
	for (int i = 1; i < list->length; i++)
		if (list->elements[i].from != list->elements[outPos - 1].from)
			list->elements[outPos++] = list->elements[i];
	list->length = outPos;

	// reverse list if necessary
	if (sortOrder == XPATH_REVERSE_DOCUMENT_ORDER) {
		for (int i = 0; i < outPos / 2; i++) {
			XMLElement swapper = list->elements[i];
			list->elements[i] = list->elements[outPos - 1 - i];
			list->elements[outPos - 1 - i] = swapper;
		}
	}
} // end of XPath_sortElementList(XMLElementList*, int)


XPathData * XPath_createXPathData(Index *index, char *stringValue) {
	XPathData *result = (XPathData*)malloc(sizeof(XPathData));
	result->index = index;
	result->dataType = XPATH_STRING;
	result->stringValue = stringValue;
	return result;
} // end of XPath_createXPathData(Index*, char*)


XPathData * XPath_createXPathData(Index *index, float numberValue) {
	XPathData *result = (XPathData*)malloc(sizeof(XPathData));
	result->index = index;
	result->dataType = XPATH_NUMBER;
	result->numberValue = numberValue;
	return result;
} // end of XPath_createXPathData(Index*, float)


XPathData * XPath_createXPathData(Index *index, bool booleanValue) {
	XPathData *result = (XPathData*)malloc(sizeof(XPathData));
	result->index = index;
	result->dataType = XPATH_BOOLEAN;
	result->booleanValue = booleanValue;
	return result;
} // end of XPath_createXPathData(Index*, bool)


void XPath_deleteXPathData(XPathData *data) {
	switch (data->dataType) {
		case XPATH_NODESET:
			XPath_deleteElementList(data->nodeValue);
			break;
		case XPATH_STRING:
			free(data->stringValue);
			break;
	}
	free(data);
} // end of XPath_deleteXPathData(XPathData*)


/** Adds the offset given by "off" to the string given by "string". **/
static void strcatOffset(char *string, offset off) {
	long long value = off;
	sprintf(&string[strlen(string)], "%lld", value);
} // end of strcatOffset(char*, offset)


char * XPath_getElement(Index *index, offset from, offset to) {
	char arguments[64];
	arguments[0] = 0;
	strcatOffset(arguments, from);
	strcat(arguments, " ");
	strcatOffset(arguments, to);
	GetQuery *gq =
		new GetQuery(index, "get", EMPTY_MODIFIERS, arguments, Index::GOD, -1);
	gq->parse();
	char *result = (char*)malloc(4096);
	int resultAllocated = 4096;
	int resultLength = 0;
	char *line = (char*)malloc(FilteredInputStream::MAX_FILTERED_RANGE_SIZE);
	int lineLength = 0;
	while (gq->getNextLine(line)) {
		lineLength = strlen(line);
		if (lineLength + resultLength + 1 > resultAllocated) {
			resultAllocated = ((lineLength + resultLength + 1) * 3) / 2;
			result = (char*)realloc(result, resultAllocated);
		}
		strcpy(&result[resultLength], line);
		resultLength += lineLength;
	}
	free(line);
	delete gq;
	return result;
} // end of XPath_getElement(Index*, offset, offset)


/**
 * XPath_getText returns all TEXT that belongs to the element starting at
 * "from" and ending at "to". The "level" informatio is needed to exclude
 * child elements.
 **/
char * XPath_getText(Index *index, offset from, offset to, int level) {
	offset start, end;
	char openTag[32], closeTag[32];
	sprintf(openTag, "<level!%i>", level + 1);
	sprintf(closeTag, "</level!%i>", level + 1);
	ExtentList *openList = index->getPostings(openTag, Index::GOD);
	ExtentList *closeList = index->getPostings(closeTag, Index::GOD);
	offset position = from + 1;
	char *result = duplicateString("");
	int resultLength = 0;
	while (position < to) {
		offset continueUntil = to;
		if (openList->getFirstStartBiggerEq(position, &start, &end))
			if (start <= continueUntil)
				continueUntil = start - 1;
		if (continueUntil >= position) {
			char *string = XPath_getElement(index, position, continueUntil);
			int stringLength = strlen(string);
			result = (char*)realloc(result, resultLength + stringLength + 2);
			strcpy(&result[resultLength], string);
			free(string);
			resultLength += stringLength;
		}
		if (closeList->getFirstStartBiggerEq(start, &start, &end))
			position = end + 1;
		else
			position = MAX_OFFSET;
	}
	delete openList;
	delete closeList;
	return result;
} // end of XPath_getText(Index*, offset, offset)


char * XPath_extractString(XPathData *argument) {
	if (argument->dataType == XPATH_STRING)
		return duplicateString(argument->stringValue);
	else {
		XPathData *dummy = XPath_string(argument);
		char *result = duplicateString(dummy->stringValue);
		XPath_deleteXPathData(dummy);
		return result;
	}
} // end of XPath_extractString(XPathData*)


float XPath_extractNumber(XPathData *argument) {
	if (argument->dataType == XPATH_NUMBER)
		return argument->numberValue;
	else {
		XPathData *dummy = XPath_number(argument);
		float result = dummy->numberValue;
		XPath_deleteXPathData(dummy);
		return result;
	}
} // end of extractNumber(XPathData*)


XPathData * XPath_string(XPathData *argument) {
	XPathData *result = (XPathData*)malloc(sizeof(XPathData));
	result->dataType = XPATH_STRING;
	result->index = argument->index;
	switch (argument->dataType) {
		case XPATH_NODESET:
			if (argument->nodeValue->length == 0) {
				// node set is empty: return empty string
				result->stringValue = duplicateString("");
			}
			else {
				// node set is non-empty: return string representation of first node
				result->stringValue =
					XPath_getText(argument->index,
					              argument->nodeValue->elements[0].from,
					              argument->nodeValue->elements[0].to, 
					              argument->nodeValue->elements[0].level);
			}
			break;
		case XPATH_STRING:
			result->stringValue = duplicateString(argument->stringValue);
			break;
		case XPATH_BOOLEAN:
			if (argument->booleanValue)
				result->stringValue = duplicateString("true");
			else
				result->stringValue = duplicateString("false");
			break;
		case XPATH_NUMBER:
			if (isNAN(argument->numberValue))
				result->stringValue = duplicateString("NaN");
			else if (isINF(argument->numberValue))
				result->stringValue = duplicateString("Infinity");
			else if (fabs(argument->numberValue) <= XPATH_EPSILON)
				result->stringValue = duplicateString("0");
			else if (fabs(argument->numberValue - argument->numberValue) <= XPATH_EPSILON) {
				// if the number is an integer, return the integer representation
				char number[64];
				sprintf(number, "%.0f", argument->numberValue);
				result->stringValue = duplicateString(number);
			}
			else {
				// otherwise, print the decimal "." representation of the number
				char number[64];
				sprintf(number, "%.9f", argument->numberValue);
				int len = strlen(number);
				while ((number[len - 1] == '0') && (number[len - 2] != '.')) {
					if (--len == 3)
						break;
				}
				number[len] = 0;
				result->stringValue = duplicateString(number);
			}
			break;
		default:
			result->stringValue = duplicateString("");
			result->dataType = XPATH_TYPE_ERROR;
			break;
	}
	return result;
} // end of XPath_string(XPathData*)


XPathData * XPath_concat(XPathData **arguments) {
	Index *index = arguments[0]->index;
	char *resultString = duplicateString("");
	for (int i = 0; arguments[i] != NULL; i++) {
		if (arguments[i]->dataType == XPATH_STRING) {
			char *newString = concatenateStrings(resultString, arguments[i]->stringValue);
			free(resultString);
			resultString = newString;
		}
		else {
			XPathData *dummy = XPath_string(arguments[i]);
			char *newString = concatenateStrings(resultString, dummy->stringValue);
			XPath_deleteXPathData(dummy);
			free(resultString);
			resultString = newString;
		}
	}
	return XPath_createXPathData(index, resultString);
} // end of XPath_concat(XPathData**)


XPathData * XPath_starts_with(XPathData *argument1, XPathData *argument2) {
	char *s1 = XPath_extractString(argument1);
	char *s2 = XPath_extractString(argument2);
	bool result = (strstr(s1, s2) == s1);
	free(s1);
	free(s2);
	return XPath_createXPathData(argument1->index, result);
} // end of XPath_starts_with(XPathData*, XPathData*)


XPathData * XPath_contains(XPathData *argument1, XPathData *argument2) {
	char *s1 = XPath_extractString(argument1);
	char *s2 = XPath_extractString(argument2);
	bool result = (strstr(s1, s2) != NULL);
	free(s1);
	free(s2);
	return XPath_createXPathData(argument1->index, result);
} // end of XPath_contains(XPathData*, XPathData*)


XPathData * XPath_substring_before(XPathData *argument1, XPathData *argument2) {
	char *s1 = XPath_extractString(argument1);
	char *s2 = XPath_extractString(argument2);
	char *match = strstr(s1, s2);
	if (match == NULL)
		s1[0] = 0;
	else
		match[0] = 0;
	free(s2);
	return XPath_createXPathData(argument1->index, s1);
} // end of XPath_substring_before(XPathData*, XPathData*)


XPathData * XPath_substring_after(XPathData *argument1, XPathData *argument2) {
	char *s1 = XPath_extractString(argument1);
	char *s2 = XPath_extractString(argument2);
	char *match = strstr(s1, s2);
	char *result;
	if (match == NULL)
		result = duplicateString("");
	else
		result = duplicateString(&match[strlen(s2)]);
	free(s1);
	free(s2);
	return XPath_createXPathData(argument1->index, result);
} // end of XPath_substring_after(XPathData*, XPathData*)


XPathData * XPath_substring(XPathData *argument, int startPosition, int length) {
	char *s = XPath_extractString(argument);
	int sLen = strlen(s);
	if ((startPosition >= sLen) || (startPosition < 0)) {
		free(s);
		return XPath_createXPathData(argument->index, duplicateString(""));
	}
	else if (startPosition == 0) {
		if (length <= sLen)
			s[length] = 0;
		return XPath_createXPathData(argument->index, s);
	}
	else {
		int pos = startPosition;
		while ((s[pos] != 0) && (pos < startPosition + length)) {
			s[pos - startPosition] = s[pos];
			pos++;
		}
		s[pos - startPosition] = 0;
		return XPath_createXPathData(argument->index, s);
	}
} // end of XPath_substring(XPathData*, int, int)


XPathData * XPath_string_length(XPathData *argument) {
	float result;
	if (argument->dataType == XPATH_STRING)
		result = strlen(argument->stringValue);
	else {
		char *s = XPath_extractString(argument);
		result = strlen(s);
		free(s);
	}
	return XPath_createXPathData(argument->index, result);
} // end of XPath_string_length(XPathData*)


XPathData * XPath_normalize_space(XPathData *argument) {
	char *s = XPath_extractString(argument);
	int inPos = 0;
	int outPos = 0;
	while ((s[inPos] > 0) && (s[inPos] <= ' '))
		inPos++;
	while (s[inPos] != 0) {
		char c = s[inPos];
		s[outPos++] = s[inPos++];
		if ((c > 0) && (c <= ' ')) {
			while ((s[inPos] > 0) && (s[inPos] <= ' '))
				inPos++;
		}
	}
	s[outPos] = 0;
	if (outPos > 0) {
		if ((s[outPos - 1] > 0) && (s[outPos - 1] <= ' '))
			s[--outPos] = 0;
	}
	return XPath_createXPathData(argument->index, s);
} // end of XPath_normalize_space(XPathData*)


XPathData * XPath_translate(XPathData *argument, XPathData *from, XPathData *to) {
	char *string = XPath_extractString(argument);
	char *fromString = XPath_extractString(from);
	char *toString = XPath_extractString(to);
	byte translation[256];
	memset(translation, 0, 256);
	for (int i = 0; (fromString[i] != 0) && (toString[i] != 0); i++) {
		byte a = (byte)fromString[i];
		byte b = (byte)toString[i];
		translation[a] = b;
	}
	free(fromString);
	free(toString);
	int inPos = 0;
	int outPos = 0;
	while (string[inPos] != 0) {
		byte b = (byte)string[inPos++];
		if (translation[b] != 0)
			string[outPos++] = (char)translation[b];
	}
	string[outPos] = 0;
	return XPath_createXPathData(argument->index, string);
} // end of XPath_translate(XPathData*, XPathData*, XPathData*)


XPathData * XPath_boolean(XPathData *argument) {
	bool result;
	switch (argument->dataType) {
		case XPATH_STRING:
			result = (strlen(argument->stringValue) > 0);
			break;
		case XPATH_BOOLEAN:
			result = argument->booleanValue;
			break;
		case XPATH_NUMBER:
			if ((fabs(argument->numberValue) >= XPATH_EPSILON) && (!isNAN(argument->numberValue)))
				result = true;
			else
				result = false;
			break;
		case XPATH_NODESET:
			result = (argument->nodeValue->length > 0);
			break;
		case XPATH_TYPE_ERROR:
			result = false;
			break;
	}
	return XPath_createXPathData(argument->index, result);
} // end of XPath_boolean(XPathData*)


XPathData * XPath_not(XPathData *argument) {
	XPathData *result = XPath_boolean(argument);
	result->booleanValue = !(result->booleanValue);
	return result;
} // end of XPath_not(XPathData*)


XPathData * XPath_true(Index *index) {
	return XPath_createXPathData(index, true);
} // end of XPath_true()


XPathData * XPath_false(Index *index) {
	return XPath_createXPathData(index, false);
} // end of XPath_false()


XPathData * XPath_number(XPathData *argument) {
	char *s;
	float result;
	switch (argument->dataType) {
		case XPATH_STRING:
			if (sscanf(argument->stringValue, "%f", &result) != 1)
				result = 0; // NAN;
			break;
		case XPATH_BOOLEAN:
			if (argument->booleanValue)
				result = 1.0;
			else
				result = 0.0;
			break;
		case XPATH_NUMBER:
			result = argument->numberValue;
			break;
		case XPATH_NODESET:
			s = XPath_extractString(argument);
			if (sscanf(argument->stringValue, "%f", &result) != 1)
				result = 0; // NAN;
			free(s);
			break;
		default:
			result = 0; // NAN;
			break;
	}
	return XPath_createXPathData(argument->index, result);
} // end of XPath_number(XPathData*)


XPathData * XPath_sum(XPathData **arguments) {
	float result = 0.0;
	for (int i = 0; arguments[i] != NULL; i++) {
		if (arguments[i]->dataType == XPATH_NUMBER)
			result += arguments[i]->numberValue;
		else {
			float number = XPath_extractNumber(arguments[i]);
			result += number;
		}
	}
	return XPath_createXPathData(arguments[0]->index, result);
} // end of XPath_sum(XPathData**)


XPathData * XPath_floor(XPathData *argument) {
	float number;
	if (argument->dataType == XPATH_NUMBER)
		number = argument->numberValue;
	else
		number = XPath_extractNumber(argument);
	return XPath_createXPathData(argument->index, (float)floor(number));
} // end of XPath_floor(XPathData*)


XPathData * XPath_ceiling(XPathData *argument) {
	float number;
	if (argument->dataType == XPATH_NUMBER)
		number = argument->numberValue;
	else
		number = XPath_extractNumber(argument);
	return XPath_createXPathData(argument->index, (float)ceil(number));
} // end of XPath_ceiling(XPathData*)


XPathData * XPath_round(XPathData *argument) {
	float number;
	if (argument->dataType == XPATH_NUMBER)
		number = argument->numberValue;
	else
		number = XPath_extractNumber(argument);
	return XPath_createXPathData(argument->index, (float)LROUND(number));
} // end of XPath_round(XPathData*)


XPathData * XPath_product(XPathData **arguments) {
	float result = 1.0;
	for (int i = 0; arguments[i] != NULL; i++) {
		if (arguments[i]->dataType == XPATH_NUMBER)
			result *= arguments[i]->numberValue;
		else {
			float number = XPath_extractNumber(arguments[i]);
			result *= number;
		}
	}
	return XPath_createXPathData(arguments[0]->index, result);
} // end of XPath_product(XPathData**)


XPathData * XPath_difference(XPathData **arguments) {
	float result = XPath_extractNumber(arguments[0]);
	for (int i = 1; arguments[i] != NULL; i++) {
		if (arguments[i]->dataType == XPATH_NUMBER)
			result -= arguments[i]->numberValue;
		else {
			float number = XPath_extractNumber(arguments[i]);
			result -= number;
		}
	}
	return XPath_createXPathData(arguments[0]->index, result);
} // end of XPath_difference(XPathData**)


XPathData * XPath_division(XPathData **arguments) {
	float result = XPath_extractNumber(arguments[0]);
	for (int i = 1; arguments[i] != NULL; i++) {
		float number = XPath_extractNumber(arguments[i]);
		if (number == 0.0) {
			result = 0; // NAN;
			break;
		}
		else
			result /= number;
	}
	return XPath_createXPathData(arguments[0]->index, result);
} // end of XPath_division(XPathData**)


XPathData * XPath_modulo(XPathData **arguments) {
	int result = (int)LROUND(XPath_extractNumber(arguments[0]));
	for (int i = 1; arguments[i] != NULL; i++) {
		float number = XPath_extractNumber(arguments[i]);
		int i = (int)LROUND(number);
		if (i == 0) {
			result = 0;
			break;
		}
		else
			result %= i;
	}
	float resultValue = result;
	return XPath_createXPathData(arguments[0]->index, resultValue);
} // end of XPath_modulo(XPathData**)


XPathData * XPath_compare(XPathData *argument1, XPathData *argument2, int comparison) {
	Index *index = argument1->index;
	bool result;

	// first type of comparison: node-set <> node-set
	if ((argument1->dataType == XPATH_NODESET) && (argument2->dataType == XPATH_NODESET)) {
		int length1 = argument1->nodeValue->length;
		char **strings1 = (char**)malloc(length1 * sizeof(char*));
		for (int i = 0; i < length1; i++)
			strings1[i] = NULL;
		int length2 = argument2->nodeValue->length;
		char **strings2 = (char**)malloc(length2 * sizeof(char*));
		for (int i = 0; i < length2; i++)
			strings2[i] = NULL;
		bool found = false;
		for (int j = 0; (j < length1) && (!found); j++) {
			if (strings1[j] == NULL)
				strings1[j] = XPath_getText(index,
				                            argument1->nodeValue->elements[j].from,
				                            argument1->nodeValue->elements[j].to,
				                            argument1->nodeValue->elements[j].level);
			for (int k = 0; (k < length2) && (!found); k++) {
				if (strings2[k] == NULL)
					strings2[k] = XPath_getText(index,
					                            argument2->nodeValue->elements[k].from,
					                            argument2->nodeValue->elements[k].to,
					                            argument2->nodeValue->elements[k].level);
				if (XPath_compare(strings1[j], strings2[k], comparison))
					found = true;
			}
		}
		for (int i = 0; i < length1; i++)
			if (strings1[i] != NULL)
				free(strings1[i]);
		free(strings1);
		for (int i = 0; i < length2; i++)
			if (strings2[i] != NULL)
				free(strings2[i]);
		free(strings2);
		result = found;
	} // end if ((argument1->dataType == XPATH_NODESET) && (argument2->dataType == XPATH_NODESET))

	// second type of comparison: node-set <> anything else
	else if ((argument1->dataType == XPATH_NODESET) || (argument2->dataType == XPATH_NODESET)) {
		XMLElementList *firstArgument;
		XPathData *secondArgument;
		if (argument1->dataType == XPATH_NODESET) {
			firstArgument = argument1->nodeValue;
			secondArgument = argument2;
		}
		else {
			firstArgument = argument2->nodeValue;
			secondArgument = argument1;
			switch (comparison) {
				case XPATH_LT:
					comparison = XPATH_GTE;
					break;
				case XPATH_GT:
					comparison = XPATH_LTE;
					break;
				case XPATH_LTE:
					comparison = XPATH_GT;
					break;
				case XPATH_GTE:
					comparison = XPATH_LT;
					break;
			}
		}
		int length = firstArgument->length;
		bool found = false;
		XPathData *dummy1, *dummy2;
		for (int i = 0; (i < length) && (!found); i++) {
			char *string = XPath_getText(index,
			                             firstArgument->elements[i].from,
			                             firstArgument->elements[i].to,
			                             firstArgument->elements[i].level);
			switch (secondArgument->dataType) {
				case XPATH_STRING:
					if (XPath_compare(string, secondArgument->stringValue, comparison))
						found = true;
					break;
				case XPATH_NUMBER:
					dummy1 = XPath_createXPathData(index, duplicateString(string));
					dummy2 = XPath_number(dummy1);
					if (XPath_compare(dummy2->numberValue, secondArgument->numberValue, comparison))
						found = true;
					XPath_deleteXPathData(dummy2);
					XPath_deleteXPathData(dummy1);
					break;
				case XPATH_BOOLEAN:
					dummy1 = XPath_createXPathData(index, duplicateString(string));
					dummy2 = XPath_boolean(dummy1);
					if (XPath_compare(dummy2->booleanValue, secondArgument->booleanValue, comparison))
						found = true;
					XPath_deleteXPathData(dummy2);
					XPath_deleteXPathData(dummy1);
					break;
			} // end switch (argument2->dataType)
			free(string);
		}
		result = found;
	} // end if ((argument1->dataType == XPATH_NODESET) || (argument2->dataType == XPATH_NODESET))

	// third type of comparison: "==" and "!=" for non-node-sets
	else if ((comparison == XPATH_EQ) || (comparison == XPATH_NEQ)) {
		if ((argument1->dataType == XPATH_BOOLEAN) || (argument2->dataType == XPATH_BOOLEAN)) {
			XPathData *arg1 = XPath_boolean(argument1);
			XPathData *arg2 = XPath_boolean(argument2);
			result = XPath_compare(arg1->booleanValue, arg2->booleanValue, comparison);
			XPath_deleteXPathData(arg2);
			XPath_deleteXPathData(arg1);
		}
		else if ((argument1->dataType == XPATH_NUMBER) || (argument2->dataType == XPATH_NUMBER)) {
			XPathData *arg1 = XPath_number(argument1);
			XPathData *arg2 = XPath_number(argument2);
			result = XPath_compare(arg1->numberValue, arg2->numberValue, comparison);
			XPath_deleteXPathData(arg2);
			XPath_deleteXPathData(arg1);
		}
		else if ((argument1->dataType == XPATH_STRING) || (argument2->dataType == XPATH_STRING)) {
			XPathData *arg1 = XPath_string(argument1);
			XPathData *arg2 = XPath_string(argument2);
			result = XPath_compare(arg1->stringValue, arg2->stringValue, comparison);
			XPath_deleteXPathData(arg2);
			XPath_deleteXPathData(arg1);
		}
		else
			result = false;
	} // end if ((comparison == XPATH_EQ) || (comparison == XPATH_NEQ))

	// fourth type of comparison: "<=", "<", ">=", ">" on non-node-sets
	else {
		XPathData *arg1 = XPath_number(argument1);
		XPathData *arg2 = XPath_number(argument2);
		result = XPath_compare(arg1->numberValue, arg2->numberValue, comparison);
		XPath_deleteXPathData(arg2);
		XPath_deleteXPathData(arg1);
	}

	return XPath_createXPathData(index, result);
} // end of XPath_compare(XPathData*, XPathData*, int)


bool XPath_compare(XMLElementList *list1, XMLElementList *list2, int comparison) {
	return false;
} // end of XPath_compare(XMLElementList*, XMLElementList*, int)


bool XPath_compare(char *string1, char *string2, int comparison) {
	int result = strcasecmp(string1, string2);
	switch (comparison) {
		case XPATH_EQ:
			return (result == 0);
		case XPATH_NEQ:
			return (result != 0);
		case XPATH_LT:
			return (result < 0);
		case XPATH_GT:
			return (result > 0);
		case XPATH_LTE:
			return (result <= 0);
		case XPATH_GTE:
			return (result >= 0);
		default:
			return false;
	}
} // end of XPath_compare(char*, char*, int)


bool XPath_compare(bool bool1, bool bool2, int comparison) {
	int value1 = (bool1 ? 1 : 0);
	int value2 = (bool2 ? 1 : 0);
	switch (comparison) {
		case XPATH_EQ:
			return (value1 == value2);
		case XPATH_NEQ:
			return (value1 != value2);
		case XPATH_LT:
			return (value1 < value2);
		case XPATH_GT:
			return (value1 > value2);
		case XPATH_LTE:
			return (value1 <= value2);
		case XPATH_GTE:
			return (value1 >= value2);
		default:
			return false;
	}
} // end of XPath_compare(bool, bool, int)


bool XPath_compare(float number1, float number2, int comparison) {
	switch (comparison) {
		case XPATH_EQ:
			return (fabs(number1 - number2) <= XPATH_EPSILON);
		case XPATH_NEQ:
			return (fabs(number1 - number2) > XPATH_EPSILON);
		case XPATH_LT:
			return (number1 < number2 - XPATH_EPSILON);
		case XPATH_GT:
			return (number1 > number2 + XPATH_EPSILON);
		case XPATH_LTE:
			return (number1 <= number2 + XPATH_EPSILON);
		case XPATH_GTE:
			return (number1 >= number2 - XPATH_EPSILON);
		default:
			return false;
	}
} // end of XPath_compare(float, float, int)




