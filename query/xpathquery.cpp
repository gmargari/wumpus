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
 * Implementation of the XPath stuff.
 *
 * author: Stefan Buettcher
 * created: 2004-11-30
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <string.h>
#include "xpathquery.h"
#include "xpath_predicate.h"
#include "xpath_primitives.h"
#include "xpath_tokenizer.h"
#include "getquery.h"
#include "../filemanager/filemanager.h"
#include "../filters/inputstream.h"
#include "../filters/xml_inputstream.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"


void XPathQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	errorMessage = NULL;
	resultList = NULL;
	currentResultPosition = 0;
	syntaxError = false;
	processModifiers(modifiers);

	openingTagsOnLevel = typed_malloc(ExtentList*, MAX_NESTING_LEVEL + 1);
	closingTagsOnLevel = typed_malloc(ExtentList*, MAX_NESTING_LEVEL + 1);
	for (int i = 0; i < MAX_NESTING_LEVEL; i++) {
		char openString[32], closeString[32];
		sprintf(openString, "<level!%d>", i);
		sprintf(closeString, "</level!%d>", i);
		openingTagsOnLevel[i] = index->getPostings(openString, Index::GOD);
		closingTagsOnLevel[i] = index->getPostings(closeString, Index::GOD);
		if (openingTagsOnLevel[i]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
			for (int k = i + 1; k < MAX_NESTING_LEVEL; k++) {
				openingTagsOnLevel[k] = new ExtentList_Empty();
				closingTagsOnLevel[k] = new ExtentList_Empty();
			}
			break;
		}
	}
	openingTagsOnLevel[MAX_NESTING_LEVEL] = new ExtentList_Empty();
	closingTagsOnLevel[MAX_NESTING_LEVEL] = new ExtentList_Empty();

	// first, check if the initial part of the XPath query has the right form:
	// doc("URI")/...
	queryString = duplicateString(body);
	int closingBracket = -1;
	bool quotesOk = true;
	int quoteCnt = 0;
	for (int i = 0; queryString[i] != 0; i++) {
		if (queryString[i] == '"')
			quoteCnt++;	
		else if (queryString[i] == '(') {
			if (queryString[i + 1] != '"')
				quotesOk = false;
		}
		else if (queryString[i] == ')') {
			if (queryString[i - 1] != '"')
				quotesOk = false;
			closingBracket = i;
			break;
		}
	}

	// check whether we actually have XPath information in the index
	if (openingTagsOnLevel[0]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
		errorMessage = duplicateString("XPath not supported by index. Set ENABLE_XPATH=true when building the index.");
		return;
	}

	// check for basic syntax errors
	if ((!quotesOk) || (quoteCnt != 2) || (closingBracket <= 6)) {
		FREE_AND_SET_TO_NULL(queryString);
		return;
	}
	if (queryString[closingBracket + 1] != '/') {
		FREE_AND_SET_TO_NULL(queryString);
		return;
	}

	// first part seems to be ok; obtain the index extent that corresponds to the file given
	char *URL = getSubstring(queryString, strlen("doc(\""), closingBracket - 1);
	IndexedINodeOnDisk iiod;
	bool ok = (visibleExtents != NULL);
	if (ok)
		ok = (visibleExtents->getFileManager() != NULL);
	if (ok)
		ok = visibleExtents->getFileManager()->getINodeInfo(URL, &iiod);
	free(URL);
	if (!ok) {
		errorMessage = duplicateString("File not found.");
		return;
	}
	offset fileStart = iiod.coreData.startInIndex;
	offset fileEnd = (fileStart + iiod.coreData.tokenCount) - 1;

	// check if the user has the necessary permissions to search the file
	ExtentList *list = visibleExtents->getExtentList();
	offset s, e;
	ok = list->getLastStartSmallerEq(fileStart, &s, &e);
	delete list;
	if ((!ok) || (e < fileEnd)) {
		errorMessage = duplicateString("File not found.");
		return;
	}

	// File has been found and may be accessed by this user:
	// initialize the result list to the file specified by the doc(...) stuff.
	XMLElement rootElement;
	rootElement.from = fileStart;
	rootElement.to = fileEnd;
	rootElement.level = 0;
	resultList = XPath_createEmptyElementList();
	XPath_addToElementList(resultList, rootElement);

	free(queryString);
	queryString = toCanonicalForm(body);
	if (queryString == NULL)
		return;

	executeQuery();
} // end of initialize(...)


XPathQuery::XPathQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents);
	mustFreeVisibleExtentsInDestructor = false;
} // end of XPathQuery(...)


XPathQuery::XPathQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visExt =
		index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visExt);
	mustFreeVisibleExtentsInDestructor = true;
} // end of XPathQuery(Index*, char*, char**, char*, uid_t, int)


XPathQuery::~XPathQuery() {
	FREE_AND_SET_TO_NULL(queryString);
	FREE_AND_SET_TO_NULL(errorMessage);
	if (resultList != NULL) {
		XPath_deleteElementList(resultList);
		resultList = NULL;
	}
	if (openingTagsOnLevel != NULL) {
		for (int i = 0; i <= MAX_NESTING_LEVEL; i++) {
			delete openingTagsOnLevel[i];
			delete closingTagsOnLevel[i];
		}
		free(openingTagsOnLevel);
		free(closingTagsOnLevel);
		openingTagsOnLevel = NULL;
	}
} // end of ~XPathQuery()


void XPathQuery::executeQuery() {
	if (resultList == NULL)
		return;

	// use XPathTokenizer to decompose the query into its individual components
	XPathTokenizer *tok = new XPathTokenizer(queryString);
	// remove the first step ("doc(...)") because it has already been processed
	tok->getNext();
	while (tok->hasNext()) {
		char *step = duplicateString(tok->getNext());
		char *colons = strstr(step, "::");
		assert(colons != NULL);
		colons[0] = 0;
		char *axis = step;
		char *nodeTest = duplicateString(&colons[2]);
		char *openBracket = strchr(nodeTest, '[');
		char *predicates;
		if (openBracket == NULL)
			predicates = duplicateString("");
		else {
			predicates = duplicateString(openBracket);
			openBracket[0] = 0;
		}

		// perform the necessary operations for this step of the XPath
		if (resultList->length > 0) {
			XMLElementList *newResultList = XPath_createEmptyElementList();
			for (int i = 0; i < resultList->length; i++) {
				XMLElementList *tempResultList =
					processQueryStep(axis, nodeTest, predicates, resultList, i);
				XPath_addToElementList(newResultList, *tempResultList);
				XPath_deleteElementList(tempResultList);
			}
			XPath_sortElementList(newResultList, XPATH_DOCUMENT_ORDER);
			XPath_deleteElementList(resultList);
			resultList = newResultList;
		}

		free(axis);
		free(nodeTest);
		free(predicates);
	}
	delete tok;
} // end of executeQuery()


XMLElementList * XPathQuery::processQueryStep(
		char *axis, char *nodeTest, char *predicates, XMLElementList *current, int listPosition) {
	XMLElementList *result;
	ExtentList *nodeTestOpen;
	ExtentList *nodeTestClose;
	if ((strcasecmp(nodeTest, "*") == 0) || (strcasecmp(nodeTest, "node()") == 0)) {
		nodeTestOpen = new ExtentList_Range(1, MAX_OFFSET);
		nodeTestClose = new ExtentList_Range(1, MAX_OFFSET);
	}
	else {
		char openString[MAX_TOKEN_LENGTH * 2];
		char closeString[MAX_TOKEN_LENGTH * 2];
		if (strlen(nodeTest) > MAX_TOKEN_LENGTH) {
			nodeTestOpen = new ExtentList_Empty();
			nodeTestClose = new ExtentList_Empty();
		}
		else {
			if (strcasecmp(axis, "attribute") == 0) {
				sprintf(openString, "<attr!%s>", nodeTest);
				sprintf(closeString, "</attr!%s>", nodeTest);
			}
			else {
				sprintf(openString, "<%s>", nodeTest);
				sprintf(closeString, "</%s>", nodeTest);
			}
			nodeTestOpen = index->getPostings(openString, Index::GOD);
			nodeTestClose = index->getPostings(closeString, Index::GOD);
		}
	}
	int currentLevel = current->elements[listPosition].level;
	if (strcasecmp(axis, "self") == 0)
		result = getAncestors(nodeTestOpen, nodeTestClose, current, listPosition, currentLevel, currentLevel);
	else if (strcasecmp(axis, "parent") == 0)
		result = getAncestors(nodeTestOpen, nodeTestClose, current, listPosition, currentLevel - 1, currentLevel - 1);
	else if (strcasecmp(axis, "ancestor") == 0)
		result = getAncestors(nodeTestOpen, nodeTestClose, current, listPosition, 0, currentLevel - 1);
	else if (strcasecmp(axis, "ancestor-or-self") == 0)
		result = getAncestors(nodeTestOpen, nodeTestClose, current, listPosition, 0, currentLevel);
	else if (strcasecmp(axis, "child") == 0)
		result = getDescendants(nodeTestOpen, nodeTestClose, current, listPosition, currentLevel + 1, currentLevel + 1);
	else if (strcasecmp(axis, "descendant") == 0)
		result = getDescendants(nodeTestOpen, nodeTestClose, current, listPosition, currentLevel + 1, 65535);
	else if (strcasecmp(axis, "descendant-or-self") == 0)
		result = getDescendants(nodeTestOpen, nodeTestClose, current, listPosition, currentLevel, 65535);
	else if (strcasecmp(axis, "attribute") == 0)
		result = getDescendants(nodeTestOpen, nodeTestClose, current, listPosition, currentLevel + 1, currentLevel + 1);
	else
		result = XPath_createEmptyElementList();

	// free memory allocated by node-test lists
	delete nodeTestOpen;
	delete nodeTestClose;

	// process predicates (if there are any)
	int pos = 0;
	while (result->length > 0) {

		// get next predicate in list of predicates
		int predStart;
		bool inQuotes;
		while ((predicates[pos] > 0) && (predicates[pos] <= ' '))
			pos++;
		if (predicates[pos] == 0)
			break;
		if (predicates[pos] != '[')
			goto syntaxErrorInProcessQueryStep;
		pos++;
		predStart = pos;
		inQuotes = false;
		while ((predicates[pos] != 0) && ((predicates[pos] != ']') || (inQuotes))) {
			if ((predicates[pos] == '"') && (predicates[pos - 1] != '\\'))
				inQuotes = !inQuotes;
			pos++;
		}
		if (predicates[pos] != ']')
			goto syntaxErrorInProcessQueryStep;
		predicates[pos] = 0;
		char *thisPredicate = duplicateString(&predicates[predStart]);
		predicates[pos] = ']';
		pos++;

		// parse the predicate ...
		XPathPredicate *predicate = new XPathPredicate(thisPredicate, index);
		free(thisPredicate);
		if (predicate->hasSyntaxError()) {
			syntaxError = true;
			continue;
		}
		
		// ... and process for all elements in the result list
		XPathData **predicateResult = predicate->apply(result);
		delete predicate;

		int outCnt = 0;
		for (int i = 0; i < result->length; i++) {
			result->elements[outCnt] = result->elements[i];
			switch (predicateResult[i]->dataType) {
				case XPATH_NUMBER:
					if (XPath_compare(i + 1, predicateResult[i]->numberValue, XPATH_EQ))
						outCnt++;
					break;
				case XPATH_BOOLEAN:
					if (predicateResult[i]->booleanValue)
						outCnt++;
					break;
				case XPATH_NODESET:
					if (predicateResult[i]->nodeValue->length > 0)
						outCnt++;
					break;
				case XPATH_STRING:
					if (strlen(predicateResult[i]->stringValue) > 0)
						outCnt++;
					break;
			}
			XPath_deleteXPathData(predicateResult[i]);
		}
		free(predicateResult);
		result->length = outCnt;

	} // end while (result->length > 0)

	goto endOfProcessQueryStep;

syntaxErrorInProcessQueryStep:
	result->length = 0;
	syntaxError = true;

endOfProcessQueryStep:
	return result;
} // end of processQueryStep(char*, char*, char*)


XMLElementList * XPathQuery::getAncestors(ExtentList *nodeTestOpen, ExtentList *nodeTestClose,
		XMLElementList *current, int pos, int minLevel, int maxLevel) {
	// initialize result structure
	XMLElementList *result = XPath_createEmptyElementList();
	offset from = current->elements[pos].from;
	offset to = current->elements[pos].to;
	int32_t level = current->elements[pos].level;

	maxLevel = MIN(maxLevel, level);
	minLevel = MAX(minLevel, 0);

	// for every possible level, obtain the element that contains the current element
	for (int l = maxLevel; l >= minLevel; l--) {
		// find the last opening tag before "from" on level "l"
		ExtentList *opening = getOpeningTagsOnLevel(l);
		offset openStart, openEnd;
		if (!opening->getLastStartSmallerEq(from, &openStart, &openEnd))
			continue;
		// find the corresponding closing tag on level "l"
		ExtentList *closing = getClosingTagsOnLevel(l);
		offset closeStart, closeEnd;
		if (!closing->getFirstStartBiggerEq(openStart, &closeStart, &closeEnd))
			continue;
		if (closeEnd < to)
			continue;
		// if we get here, we have found an element that contains the current element;
		// now, we have to check if it meets the node-test requirements
		offset s, e;
		if (!nodeTestOpen->getFirstStartBiggerEq(openStart, &s, &e))
			continue;
		if (s != openStart)
			continue;
		if (!nodeTestClose->getFirstStartBiggerEq(closeStart, &s, &e))
			continue;
		if (s != closeStart)
			continue;

		// if we get here, we have found a result element: add to list
		XMLElement newElement;
		newElement.from = openStart;
		newElement.to = closeEnd;
		newElement.level = l;
		XPath_addToElementList(result, newElement);
	}

	XPath_sortElementList(result, XPATH_REVERSE_DOCUMENT_ORDER);
	return result;
} // end of getAncestors(...)


XMLElementList * XPathQuery::getDescendants(ExtentList *nodeTestOpen, ExtentList *nodeTestClose,
		    XMLElementList *current, int pos, int minLevel, int maxLevel) {
	// initialize result structure
	XMLElementList *result = XPath_createEmptyElementList();
	offset from = current->elements[pos].from;
	offset to = current->elements[pos].to;
	int32_t level = current->elements[pos].level;

	minLevel = MAX(minLevel, level);
	maxLevel = MIN(maxLevel, MAX_NESTING_LEVEL);

	// for every possible level, obtain the element that contains the current element
	for (int l = minLevel; l <= maxLevel; l++) {
		// find all open-close pairs on level "l" within the interval ["from", "to"]
		ExtentList *opening = getOpeningTagsOnLevel(l);
		ExtentList *closing = getClosingTagsOnLevel(l);
		offset openStart, openEnd, closeStart, closeEnd;
		offset where = from;
		while (opening->getFirstStartBiggerEq(where, &openStart, &openEnd)) {
			offset s, e;
			if (openEnd > to)
				break;
			where = openStart + 1;
			// make sure that the tag we found is of the right type
			if (!nodeTestOpen->getFirstStartBiggerEq(openStart, &s, &e))
				continue;
			if (s != openStart)
				continue;
			if (!closing->getFirstStartBiggerEq(openEnd + 1, &closeStart, &closeEnd))
				break;
			if (closeEnd > to)
				break;
			// again, make sure that the tag we found is of the right type
			if (!nodeTestClose->getFirstStartBiggerEq(closeStart, &s, &e))
				continue;
			if (s != closeStart)
				continue;

			// if we get here, we have found a result element: add to list
			XMLElement newElement;
			newElement.from = openStart;
			newElement.to = closeEnd;
			newElement.level = l;
			XPath_addToElementList(result, newElement);

			where = openEnd + 1;
		}
	}

	XPath_sortElementList(result, XPATH_DOCUMENT_ORDER);
	return result;
} // end of getDescendants(...)


bool XPathQuery::isValidCommand(char *command) {
	if (strcasecmp(command, "xpath") == 0)
		return true;
	return false;
} // end of isValidCommand(char*)


bool XPathQuery::parse() {
} // end of parse()


bool XPathQuery::getNextLine(char *line) {
	if ((queryString == NULL) || (syntaxError) || (finished))
		return false;
	if ((resultList == NULL) || (currentResultPosition >= resultList->length))
		return false;

	line[0] = 0;
	char start[32], end[32];
	printOffset(resultList->elements[currentResultPosition].from, start);
	printOffset(resultList->elements[currentResultPosition].to, end);
	if (line[0] == 0)
		sprintf(line, "%s %s", start, end);
	else
		sprintf(&line[strlen(line)], " %s %s", start, end);
	if (getPathToResult) {
		char *path = getPathToExtent(resultList->elements[currentResultPosition].from,
		                             resultList->elements[currentResultPosition].to);
		if (path == NULL)
			path = duplicateString("n/a");
		sprintf(&line[strlen(line)], " %s", path);
		free(path);
	}
	currentResultPosition++;

	if (currentResultPosition >= resultList->length)
		finished = true;
	return true;
} // end of getNextLine(char*)


bool XPathQuery::getStatus(int *code, char *description) {
	if ((queryString == NULL) || (syntaxError)) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
	}
	else if (errorMessage != NULL) {
		*code = STATUS_ERROR;
		strcpy(description, errorMessage);
	}
	else {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	return true;
} // end of getStatus(int*, char*)


char * XPathQuery::toCanonicalForm(const char *query) {
	if (strstr(query, "doc(") != query) {
		// query is not syntactically correct; we won't do any canonicalization here
		return NULL;
	}
	char *result = NULL;
	XPathTokenizer *tok = new XPathTokenizer(query);
	while (tok->hasNext()) {
		char *token = tok->getNext();
		if (result == NULL) {
			result = duplicateString(token);
			continue;
		}
		char *newElement;
		if (strcmp(token, "..") == 0)
			newElement = duplicateString("parent::node()");
		else if (strcmp(token, ".") ==  0)
			newElement = duplicateString("self::node()");
		else if (strcmp(token, "") == 0)
			newElement = duplicateString("descendant-or-self::node()");
		else {
			if (token[0] == '@')
				newElement = concatenateStrings("attribute::", &token[1]);
			else if (strstr(token, "::") == NULL)
				newElement = concatenateStrings("child::", token);
			else
				newElement = duplicateString(token);
			int colonCnt = 0;
			int bracketCnt = 0;
			bool inQuotes = false;
			for (int i = 0; newElement[i] != 0; i++)
				if (newElement[i] == '"') {
					if (i == 0)
						inQuotes = !inQuotes;
					else if (newElement[i - 1] != '\\')
						inQuotes = !inQuotes;
				}
				else if (newElement[i] == ':') {
					if (!inQuotes)
						colonCnt++;
				}
				else if (newElement[i] == '[') {
					if (!inQuotes)
						bracketCnt++;
				}
				else if (newElement[i] == ']') {
					if (!inQuotes) {
						bracketCnt--;
						if (bracketCnt < 0)
							break;
					}
				}
			if (strstr(newElement, "::") == newElement)
				bracketCnt = -1;
			if (strstr(newElement, "::")[2] == 0)
				bracketCnt = -1;
			if ((colonCnt != 2) || (bracketCnt != 0)) {
				// syntax error!
				free(newElement);
				if (result != NULL)
					free(result);
				delete tok;
				return NULL;
			}
		}
		char *newResult = (char*)malloc(strlen(result) + strlen(newElement) + 2);
		sprintf(newResult, "%s/%s", result, newElement);
		free(newElement);
		free(result);
		result = newResult;
	}
	delete tok;
	return result;
} // end of toCanonicalForm(char*)


ExtentList * XPathQuery::getOpeningTagsOnLevel(int level) {
	if ((level < 0) || (level > MAX_NESTING_LEVEL))
		level = MAX_NESTING_LEVEL;
	return openingTagsOnLevel[level];
} // end of getOpeningTagsOnLevel(int)


ExtentList * XPathQuery::getClosingTagsOnLevel(int level) {
	if ((level < 0) || (level > MAX_NESTING_LEVEL))
		level = MAX_NESTING_LEVEL;
	return closingTagsOnLevel[level];
} // end of getClosingTagsOnLevel(int)


char * XPathQuery::getPathToExtent(offset start, offset end) {
	offset elementStart[MAX_NESTING_LEVEL], elementEnd[MAX_NESTING_LEVEL];
	char result[MAX_NESTING_LEVEL * (MAX_TOKEN_LENGTH + 8)];
	int resultLen = 0;
	int maxLevel = -1;

	result[resultLen] = 0;
	for (int level = 0; level < MAX_NESTING_LEVEL; level++) {
		// obtain start and end position of XML element hosting the given index extent
		offset before, after;
		ExtentList_FromTo list(
				new ExtentList_Copy(getOpeningTagsOnLevel(level)),
				new ExtentList_Copy(getClosingTagsOnLevel(level)));
		if (!list.getLastStartSmallerEq(start, &before, &after))
			break;
		if (after < end)
			break;

		elementStart[level] = before;
		elementEnd[level] = after;

		if (level == 0) {
			// on level 0, obtain name of file containing the given element
			char *fileName = NULL;
			if (visibleExtents != NULL)
				fileName = visibleExtents->getFileNameForOffset(before);
			if (fileName == NULL)
				return duplicateString("[internal error: unable to obtain file name]");
			resultLen += sprintf(&result[resultLen], "doc(\"%s\")", fileName);
			free(fileName);
		}
		else { 
			// on lower levels (greater level numbers), obtain name of XML tag
			GetQuery getQuery(index, before, before, false);
			getQuery.parse();
			char line[MAX_RESPONSELINE_LENGTH + 1];
			getQuery.getNextLine(line);

			XMLInputStream inputStream(line, strlen(line), false);
			InputToken token;
			if (!inputStream.getNextToken(&token))
				return duplicateString("[internal error: unable to obtain tag name]");
			char *tag = (char*)token.token;
			if (tag[strlen(tag) - 1] != '>')
				strcat(tag, ">");

			// count the number of siblings before the current element within the parent
			char levelTag[32];
			sprintf(levelTag, "<level!%d>", level);
			ExtentList *tempList = new ExtentList_Containment(
					index->getPostings(levelTag, Index::GOD), index->getPostings(tag, Index::GOD), true, false);
			ExtentList_Containment siblings(
					new ExtentList_OneElement(elementStart[level - 1], elementEnd[level - 1]),
					tempList, false, false);
			long long number = siblings.getCount(elementStart[level - 1], elementEnd[level]);

			// remove '<' and '>' from tag name for display to the user
			if (tag[strlen(tag) - 1] == '>')
				tag[strlen(tag) - 1] = 0;

			// print element and number among siblings to result line
			resultLen += sprintf(&result[resultLen], "/%s[%lld]", &tag[1], number);
		}

		maxLevel = level;
	}

	return duplicateString(result);
} // end of getPathToExtent(offset, offset)


void XPathQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
	getPathToResult = getModifierBool(modifiers, "getxpath", false);
} // end of processModifiers(char**)



