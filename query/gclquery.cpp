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
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <fnmatch.h>
#include <string.h>
#include "gclquery.h"
#include "getquery.h"
#include "xpathquery.h"
#include "../filters/xml_inputstream.h"
#include "../extentlist/extentlist.h"
#include "../extentlist/simplifier.h"
#include "../index/index.h"
#include "../indexcache/docidcache.h"
#include "../indexcache/extentlist_cached.h"
#include "../indexcache/indexcache.h"
#include "../misc/all.h"


void GCLQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	this->queryString = strcpy((char*)malloc(strlen(body) + 4), body);
	resultList = NULL;
	syntaxErrorDetected = false;
	hasToBeSecure = true;
	currentResultPosition = 0;
	xpathQuery = NULL;
	processModifiers(modifiers);
} // end of initialize(Index*, char*, char**, char*, VisibleExtents*)


GCLQuery::GCLQuery(Index *index, ExtentList *result) {
	initialize(index, "gcl", EMPTY_MODIFIERS, "", NULL);
	resultList = result;
} // end of GCLQuery(Index*, ExtentList*)


GCLQuery::GCLQuery(Index *index, const char *command, const char **modifiers,
		const char *body, uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents);
	if (visibleExtents != NULL)
		memoryLimit -= visibleExtents->getCount() * sizeof(VisibleExtent);
	mustFreeVisibleExtentsInDestructor = true;
	this->memoryLimit = memoryLimit;
} // end of GCLQuery(Index*, char*, char**, char*, uid_t)


GCLQuery::GCLQuery(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visExt, int memoryLimit) {
	this->userID = Index::NOBODY;
	initialize(index, command, modifiers, body, visExt);
	mustFreeVisibleExtentsInDestructor = false;
	this->memoryLimit = memoryLimit;
} // end of GCLQuery(Index*, char*, char**, char*, VisibleExtents*)


GCLQuery::~GCLQuery() {
	if (resultList != NULL) {
		delete resultList;
		resultList = NULL;
	}
	if (xpathQuery != NULL) {
		assert(getXPath);
		delete xpathQuery;
		xpathQuery = NULL;
	}
} // end of ~GCLQuery()


void GCLQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
	getText = getModifierBool(modifiers, "get", false);
	getFiltered = getModifierBool(modifiers, "filtered", false);
	getXPath = getModifierBool(modifiers, "getxpath", false);
} // end of processModifiers(char**)


bool GCLQuery::isValidCommand(const char *command) {
	if (strcasecmp(command, "gcl") == 0)
		return true;
	else
		return false;
} // end of isValidCommand(char*)


void GCLQuery::almostSecureWillDo() {
	hasToBeSecure = false;
}


bool GCLQuery::parse() {
	if (resultList != NULL) {
		syntaxErrorDetected = false;
		return true;
	}
	else if (syntaxErrorDetected)
		return false;

	// first, perform some heuristic syntax checks
	int opening = 0;
	int closing = 0;
	bool inQuotes = false;
	bool inCurly = false;
	bool inSquare = false;
	syntaxErrorDetected = true;
	for (int i = 0; queryString[i] != 0; i++) {
		switch (queryString[i]) {
			case '{':
				if (inCurly)
					return false;
				inCurly = true;
			case '[':
				if (inSquare)
					return false;
				inSquare = true;
			case '(':
				if (!inQuotes) opening++;
				break;
			case '}':
				if (!inCurly)
					return false;
				inCurly = false;
			case ']':
				if (!inSquare)
					return false;
				inSquare = false;
			case ')':
				if (!inQuotes) closing++;
				if (closing > opening)
					return false;
				break;
			case '"':
				inQuotes = !inQuotes;
				break;
		}
		if ((!inQuotes) && (!inCurly)) {
			char c = queryString[i];
			if (c <= 7)
				return false;
		}
	}
	if ((opening != closing) || (inQuotes))
		return false;

	if (useCache)
		resultList = index->getCachedList(queryString);

	if (resultList == NULL) {
		// checks went through, create the ExtentList
		resultList = parseAndReturnList(index, queryString, memoryLimit);
	}

	if (resultList == NULL) {
		return false;
	}
	else {
		syntaxErrorDetected = false;
		resultList = Simplifier::simplifyList(resultList);
		if ((visibleExtents != NULL) && (index->APPLY_SECURITY_RESTRICTIONS)) {
			if (!resultList->isAlmostSecure())
				resultList = resultList->makeAlmostSecure(visibleExtents);
			if ((hasToBeSecure) && (!resultList->isSecure()))
				resultList = visibleExtents->restrictList(resultList);
			resultList = Simplifier::simplifyList(resultList);
		}
		resultList->optimize();
		resultList = Simplifier::simplifyList(resultList);
		return true;
	}
} // end of parse()


char * GCLQuery::normalizeQueryString(const char *queryString) {
	if (queryString == NULL)
		return NULL;
	char *result = duplicateString(queryString);
	bool inQuotes = false;
	int o = 0;
	for (int i = 0; result[i] != 0; i++) {
		if ((result[i] >= 'A') && (result[i] <= 'Z'))
			result[i] += 32;
		if (result[i] == '"') {
			inQuotes = !inQuotes;
			result[o++] = result[i];
		}
		else if (inQuotes) {
			if ((result[i] > 0) && (result[i] <= ' ')) {
				if (result[o - 1] == '"')
					continue;
				if ((result[i + 1] > 0) && (result[i + 1] <= ' '))
					continue;
				if (result[i + 1] == '"')
					continue;
			}
			result[o++] = result[i];
		}
		else if ((result[i] < 0) || (result[i] > ' '))
			result[o++] = result[i];
	}
	result[o] = 0;
	while ((o > 1) && (result[0] == '(') && (result[o - 1] == ')')) {
		result[o - 1] = 0;
		char *temp = duplicateString(&result[1]);
		free(result);
		result = temp;
		o = o - 2;
	}
	return result;
} // end of normalizeQueryString(char*)


bool GCLQuery::isSimpleTerm(char *gclString) {
	while ((*gclString > 0) && (*gclString <= ' '))
		gclString++;
	int len = strlen(gclString);
	if (len <= 2)
		return false;
	while ((len > 1) && (gclString[len - 1] >= 0) && (gclString[len - 1] <= ' '))
		len--;
	if ((gclString[0] != '"') || (gclString[len - 1] != '"'))
		return false;
	for (int i = 0; i < len; i++)
		if ((gclString[i] >= 0) && (gclString[i] <= ' '))
			return false;
	int count = 0;
	XMLInputStream *tokenizer = new XMLInputStream(gclString, len, true);
	InputToken token;
	while (tokenizer->getNextToken(&token))
		count++;
	delete tokenizer;
	return (count == 1);
} // end of isSimpleTerm(char*)


ExtentList * GCLQuery::processFileRestriction(char *restriction) {
	if (visibleExtents == NULL)
		return new ExtentList_Empty();

	int allocated = 256;
	int used = 0;
	offset *start = typed_malloc(offset, allocated);
	offset *end = typed_malloc(offset, allocated);
	ExtentList *result = NULL;

	if (startsWith(restriction, "filetype")) {
		restriction = &restriction[8];
		while ((*restriction > 0) && (*restriction <= ' '))
			restriction++;
		bool ok = false;
		char inFileTypeList[FilteredInputStream::MAX_DOCUMENT_TYPE + 1];
		memset(inFileTypeList, 0, sizeof(inFileTypeList));
		if (*restriction == '=') {
			ok = true;
			restriction++;
			char *fileType = duplicateAndTrim(restriction);
			int docType = FilteredInputStream::stringToDocumentType(fileType);
			free(fileType);
			if ((docType >= 0) && (docType <= FilteredInputStream::MAX_DOCUMENT_TYPE))
				inFileTypeList[docType] = 1;
		}
		else if (startsWith(restriction, "in ")) {
			ok = true;
			restriction += 3;
			StringTokenizer *tok = new StringTokenizer(restriction, ",");
			while (tok->hasNext()) {
				char *token = duplicateAndTrim(tok->getNext());
				int docType = FilteredInputStream::stringToDocumentType(token);
				free(token);
				if ((docType >= 0) && (docType <= FilteredInputStream::MAX_DOCUMENT_TYPE))
					inFileTypeList[docType] = 1;
			}
			delete tok;
		}
		if (ok) {
			offset s = -1, e = -1;
			ExtentList *list = visibleExtents->getExtentList();
			while (list->getFirstStartBiggerEq(s + 1, &s, &e)) {
				int docType = visibleExtents->getDocumentTypeForOffset(s);
				if ((docType >= 0) && (docType <= FilteredInputStream::MAX_DOCUMENT_TYPE)) {
					if (inFileTypeList[docType]) {
						if (used >= allocated) {
							allocated = (int)(allocated * 1.31);
							start = typed_realloc(offset, start, allocated);
							end = typed_realloc(offset, end, allocated);
						}
						start[used] = s; end[used] = e; used++;
					}
				}
			}
			delete list;
		}
		goto processFileRestriction_OK;
	} // end if (startsWith(restriction, "filetype") == 0)

	if (startsWith(restriction, "filesize")) {
		restriction = &restriction[8];
		while ((*restriction > 0) && (*restriction <= ' '))
			restriction++;
		char *comparator = restriction;
		while (*restriction > ' ')
			restriction++;
		offset value;
		if (sscanf(restriction, OFFSET_FORMAT, &value) == 1) {
			offset s = -1, e = -1;
			ExtentList *list = visibleExtents->getExtentList();
			while (list->getFirstStartBiggerEq(s + 1, &s, &e)) {
				off_t fileSize = visibleExtents->getFileSizeForOffset(s);
				if (compareNumbers(fileSize, value, comparator)) {
					if (used >= allocated) {
						allocated = (int)(allocated * 1.31);
						start = typed_realloc(offset, start, allocated);
						end = typed_realloc(offset, end, allocated);
					}
					start[used] = s; end[used] = e; used++;
				}
			}
			delete list;
		}
		goto processFileRestriction_OK;
	} // end if (startsWith(restriction, "filesize"))

	if (startsWith(restriction, "filepath")) {
		restriction = &restriction[8];
		while ((*restriction > 0) && (*restriction <= ' '))
			restriction++;
		if (*restriction != '=')
			goto processFileRestriction_END;
		restriction++;
		char *pattern = duplicateAndTrim(restriction);

		offset s = -1, e = -1;
		ExtentList *list = visibleExtents->getExtentList();
		while (list->getFirstStartBiggerEq(s + 1, &s, &e)) {
			char *path = visibleExtents->getFileNameForOffset(s);
			int len = strlen(path);
			if (len == 0) {
				free(path);
				continue;
			}
			bool matches;
			if (pattern[0] == '/')
				matches = (fnmatch(pattern, path, 0) == 0);
			else {
				// only match last component of file path
				char *p = &path[len];
				while (p[-1] != '/')
					p--;
				matches = (fnmatch(pattern, p, 0) == 0);
			}
			free(path);
			if (matches) {
				if (used >= allocated) {
					allocated = (int)(allocated * 1.31);
					start = typed_realloc(offset, start, allocated);
					end = typed_realloc(offset, end, allocated);
				}
				start[used] = s; end[used] = e; used++;
			}
		}
		delete list;
		free(pattern);
		goto processFileRestriction_OK;
	} // end if (startsWith(restriction, "filepath"))

processFileRestriction_OK:
	if (used <= 0) {
		free(start);
		free(end);
		return new ExtentList_Empty();
	}
	else {
		start = typed_realloc(offset, start, used);
		end = typed_realloc(offset, end, used);
		return new ExtentList_Cached(NULL, -1, start, end, used);
	}

processFileRestriction_END:
	if (result == NULL) {
		free(start);
		free(end);
	}
	return result;
} // end of processFileRestriction(char*)


ExtentList * GCLQuery::parseAndReturnList(Index *index, char *query, int memoryLimit) {
	if (useCache) {
		ExtentList *result = index->getCachedList(query);
		if (result != NULL)
			return result;
	}

	ExtentList *firstPart = NULL;
	ExtentList *secondPart = NULL;
	char *q = normalizeString(query);
	char *ptr = NULL;

	if (strcmp(q, "\"<file!>\"..\"</file!>\"") == 0) {
		free(q);
		if (visibleExtents == NULL)
			return new ExtentList_Empty();
		else {
			ExtentList *list = visibleExtents->getExtentList();
			if (list == NULL)
				return new ExtentList_Empty();
			else
				return list;
		}
	}
	if (strcasecmp(q, "\"<document!>\"") == 0) {
		free(q);
		return getPostings("<document!>", Index::GOD);
	}
	if (strcasecmp(q, "\"</document!>\"") == 0) {
		free(q);
		return getPostings("</document!>", Index::GOD);
	}

	if (q[0] == '(') {
		// first part of the query is a subquery; find corresponding ')'
		bool inQuotes = false;
		int bracketCount = 1;
		int i = 1;
		while ((bracketCount > 0) && (q[i] != 0)) {
			switch (q[i]) {
				case '(':
					if (!inQuotes) bracketCount++;
					break;
				case ')':
					if (!inQuotes) bracketCount--;
					break;
				case '"':
					inQuotes = !inQuotes;
					break;
			}
			i++;
		}
		ptr = &q[i];
		char old = *ptr;
		*ptr = 0;
		firstPart = parseAndReturnList(index, q, memoryLimit/2);
		*ptr = old;
	}
	else if (q[0] == '[') {
		// a range string of the form "[WIDTH]"
		char *end = strchr(q, ']');
		if (end == NULL) {
			free(q);
			return NULL;
		}
		*end = 0;
		offset value;
		int result =  sscanf(&q[1], OFFSET_FORMAT, &value);
		*end = ']';
		if (result != 1) {
			free(q);
			return NULL;
		}
		else
			firstPart = new ExtentList_Range(value, index->getBiggestOffset());
		ptr = &end[1];
	}
	else if (q[0] == '{') {
		// file format restrictions, file size restrictions, etc.
		char *end = strchr(q, '}');
		if (end == NULL) {
			free(q);
			return NULL;
		}
		*end = 0;
		char *restriction = duplicateAndTrim(&q[1]);
		*end = '}';
		ExtentList *fileRestriction = processFileRestriction(restriction);
		free(restriction);
		if (fileRestriction == NULL) {
			free(q);
			return NULL;
		}
		firstPart = fileRestriction;
		ptr = &end[1];
	}
	else if ((q[0] >= '0') && (q[0] <= '9')) {
		// a number indicating a specific index position
		offset value = 0;
		int i = 0;
		while ((q[i] >= '0') && (q[i] <= '9')) {
			value = value * 10 + (q[i] - '0');
			i++;
			if (value > MAX_OFFSET) {
				free(q);
				return NULL;
			}
		}
		firstPart = new ExtentList_OneElement(value, value);
		ptr = &q[i];
	}
	else if (q[0] == '"') {
		// a list of one or more terms, enclosed in quotation marks
		int i = 1;
		while ((q[i] != 0) && (q[i] != '"'))
			i++;
		if (q[i] == '"') {
			q[i] = 0;
			firstPart = createTermSequence(index, &q[1], memoryLimit/2);
			q[i] = '"';
			ptr = &q[i + 1];
		}
	}

	if (firstPart == NULL) {
		free(q);
		return NULL;
	}
	else
		firstPart = Simplifier::simplifyList(firstPart);

	// Are we already done?
	if (ptr[0] == 0) {
		free(q);
		return firstPart;
	}

	int memoryLimitLeft = memoryLimit - firstPart->getMemoryConsumption();

	// first part has been parsed successfully; try to determine the operator
	while ((*ptr > 0) && (*ptr <= ' '))
		ptr++;
	if (strncmp(ptr, "+", 1) == 0) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[1], memoryLimitLeft));
		if (secondPart != NULL) {
			free(q);
			return new ExtentList_OR(firstPart, secondPart);
		}
	}
	else if ((strncmp(ptr, "or", 2) == 0) || (strncmp(ptr, "OR", 2) == 0)) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[2], memoryLimitLeft));
		if (secondPart != NULL) {
			free(q);
			return new ExtentList_OR(firstPart, secondPart);
		}
	}
	else if (strncmp(ptr, "^", 1) == 0) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[1], memoryLimitLeft));
		if (secondPart != NULL) {
			free(q);
			return new ExtentList_AND(firstPart, secondPart);
		}
	}
	else if ((strncmp(ptr, "and", 3) == 0) || (strncmp(ptr, "AND", 3) == 0)) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[3], memoryLimitLeft));
		if (secondPart != NULL) {
			free(q);
			return new ExtentList_AND(firstPart, secondPart);
		}
	}
	else if (strncmp(ptr, "..", 2) == 0) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[2], memoryLimitLeft));
		if (secondPart != NULL) {
			free(q);
			ExtentList *result = new ExtentList_FromTo(firstPart, secondPart);
			return Simplifier::simplifyList(result);
		}
	}
	else if ((strncmp(ptr, ">", 1) == 0) || (strncmp(ptr, "<", 1) == 0)) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[1], memoryLimitLeft));
		if (secondPart != NULL) {
			bool returnContainer = (ptr[0] == '>');
			free(q);
			if (returnContainer)
				return new ExtentList_Containment(firstPart, secondPart, true, false);
			else
				return new ExtentList_Containment(secondPart, firstPart, false, false);
		}
	}
	else if ((strncmp(ptr, "/>", 2) == 0) || (strncmp(ptr, "/<", 2) == 0)) {
		secondPart = Simplifier::simplifyList(parseAndReturnList(index, &ptr[2], memoryLimitLeft));
		if (secondPart != NULL) {
			bool returnContainer = (ptr[1] == '>');
			free(q);
			if (returnContainer)
				return new ExtentList_Containment(firstPart, secondPart, true, true);
			else
				return new ExtentList_Containment(secondPart, firstPart, false, true);
		}
	}

	// an error occurred; delete everything
	free(q);
	delete firstPart;
	return NULL;
} // end of parseAndReturnList(char*)


ExtentList * GCLQuery::createTermSequence(Index *index, char *query, int memoryLimit) {
	// use XMLInputStream to tokenizer the term sequence
	InputToken token;
	int termCount = 0;
	XMLInputStream *tokenizer = new XMLInputStream(query, strlen(query), true);
	while (tokenizer->getNextToken(&token))
		termCount++;
	delete tokenizer;
	InputToken *terms = typed_malloc(InputToken, (termCount == 0 ? 1 : termCount));
	tokenizer = new XMLInputStream(query, strlen(query), true);
	for (int i = 0; i < termCount; i++)
		tokenizer->getNextToken(&terms[i]);
	delete tokenizer;

	ExtentList **lists = typed_malloc(ExtentList*, (termCount == 0 ? 1 : termCount));

	if ((index != NULL) && (index->BIGRAM_INDEXING) && (termCount > 1)) {
		int listCount = 0;
		for (int i = 0; i < termCount; i++) {
			bool isBigram = false;
			if (i < termCount - 1) {
				char bigram[MAX_TOKEN_LENGTH * 4];
				sprintf(bigram, "%s_%s", (char*)terms[i].token, (char*)terms[i + 1].token);
				if (strlen(bigram) <= MAX_TOKEN_LENGTH) {
					lists[listCount++] = getPostings(bigram, Index::GOD);
					isBigram = true;
					if (lists[listCount - 1] != NULL) {
						if (i != termCount - 3) {
							lists[listCount - 1] = new ExtentList_Bigram(lists[listCount - 1]);
							i++;
						}
					}
				}
			}
			if (!isBigram) {
				char *term = (char*)terms[i].token;
				if (term[0] == '$') {
					// we have to take care of leading "$" (indicating stemming) because inside
					// the index we have the "$" at the end (performance when STEMMING_LEVEL==1)
					char temp[MAX_TOKEN_LENGTH * 2];
					sprintf(temp, "%s$", &term[1]);
					strcpy(term, temp);
				}
				lists[listCount++] = getPostings(term, Index::GOD);
			}
			if (lists[listCount - 1] == NULL) {
				for (int k = 0; k < listCount; k++)
					delete lists[k];
				listCount = 0;
				break;
			}
		}
		termCount = listCount;
	}
	else {
		for (int i = 0; i < termCount; i++) {
			int memoryForThis = memoryLimit / (termCount - i);
			char *term = (char*)terms[i].token;
			if (term[0] == '$') {
				// we have to take care of leading "$" (indicating stemming) because inside
				// the index we have the "$" at the end (performance when STEMMING_LEVEL==1)
				char temp[MAX_TOKEN_LENGTH * 2];
				sprintf(temp, "%s$", &term[1]);
				strcpy(term, temp);
			}
			// tell the index we are GOD, so we do not have to worry about visibility
			// at this point; the security restrictions are applied at a higher level
			lists[i] = getPostings(term, Index::GOD);
			if (lists[i] == NULL) {
				for (int k = 0; k < i; k++)
					delete lists[k];
				termCount = 0;
				break;
			}
			memoryLimit -= lists[i]->getMemoryConsumption();
		}
	}

	free(terms);

	// two possible cases: if we have a sequence of more than 1 term, we need to
	// create an ExtentList_Sequence instance; in the other case (1 term only),
	// we return the original PostingList for efficiency
	if (termCount == 0) {
		free(lists);
		return new ExtentList_Empty();
	}
	else if (termCount == 1) {
		ExtentList *result = lists[0];
		free(lists);
		return result;
	}
	else
		return new ExtentList_Sequence(lists, termCount);
} // end of createTermSequence(char*)


char * GCLQuery::normalizeString(char *s) {
	while ((*s > 0) && (*s <= ' '))
		s++;
	int len = strlen(s);

	while (s[0] == '(') {
		// first part of the query is a subquery; find corresponding ')'
		bool inQuotes = false;
		int bracketCount = 1;
		int i = 1;
		while ((bracketCount > 0) && (i < len)) {
			switch (s[i]) {
				case '(':
					if (!inQuotes) bracketCount++;
					break;
				case ')':
					if (!inQuotes) bracketCount--;
					break;
				case '"':
					inQuotes = !inQuotes;
					break;
			}
			i++;
		}
		if ((bracketCount == 0) && (i == len)) {
			s++;
			len -= 2;
		}
		else
			break;
	} // end while (s[0] == '(')

	char *result = (char*)malloc(len + 2);
	strncpy(result, s, len + 1);
	result[len] = 0;
	while ((len > 0) && (result[len - 1] > 0) && (result[len - 1] <= ' '))
		result[--len] = 0;
	char *temp = chop(result);
	free(result);
	return temp;
} // end of normalizeString(char*)


char * GCLQuery::getQueryString() {
	return duplicateString(queryString);
}


ExtentList * GCLQuery::getResult() {
	return resultList;
} // end of getResult()


bool GCLQuery::getNextLine(char *line) {
	offset start, end;
	if (resultList == NULL) {
		finished = true;
		return false;
	}
	if (--count < 0) {
		finished = true;
		return false;
	}
	if ((verbose) && (currentResultPosition == 0)) {
		char *ts = resultList->toString();
		sprintf(line, "# Query structure: %s", ts);
		free(ts);
		line = &line[strlen(line)];
		currentResultPosition = -1;
		return true;
	}
	if (!resultList->getFirstStartBiggerEq(currentResultPosition, &start, &end)) {
		finished = true;
		return false;
	}
	sprintf(line, OFFSET_FORMAT " " OFFSET_FORMAT, start, end);
	if (getText) {
		GetQuery *gq;
		if (getFiltered) {
			const char *modifiers[2] = { "filtered", NULL };
			gq = new GetQuery(index, "get", modifiers, line, visibleExtents, -1);
		}
		else
			gq = new GetQuery(index, "get", EMPTY_MODIFIERS, line, visibleExtents, -1);
		if (gq->parse()) {
			char getResult[MAX_RESPONSELINE_LENGTH];
			if (gq->getNextLine(getResult)) {
				for (int i = 0; getResult[i] != 0; i++)
					if (getResult[i] == '"')
						getResult[i] = '\'';
					else if ((getResult > 0) && (getResult[i] <= ' '))
						getResult[i] = ' ';
				getResult[MAX_GET_LENGTH] = 0;
				char *ptr = getResult;
				while ((*ptr > 0) && (*ptr <= ' '))
					ptr++;
				int len = strlen(ptr);
				while (len > 0) {
					if ((ptr[len - 1] > 0) && (ptr[len - 1] <= ' '))
						len--;
					else
						break;
				}
				ptr[len] = 0;
				sprintf(&line[strlen(line)], " \"%s\"", ptr);
			}
			else
				strcat(line, " \"\"");
		}
		else
			strcat(line, " \"n/a\"");
		delete gq;
	}

	if (printDocumentID) {
		char docID[256];
		getDocIdForOffset(docID, start, end, false);
		sprintf(&line[strlen(line)], " \"%s\"", docID);
	}

	if (printFileName)
		addFileNameToResultLine(line, start);
	if (printPageNumber)
		addPageNumberToResultLine(line, start, end);

	if (getXPath) {
		// obtain XPath expression if desired
		char *path = NULL;
		if (visibleExtents != NULL) {
			static const char *MODIFIERS[2] = { "getxpath", NULL };
			if (xpathQuery == NULL)
				xpathQuery = new XPathQuery(index, "xpath", MODIFIERS, "", visibleExtents, -1);
			path = xpathQuery->getPathToExtent(start, end);
		}
		if (path == NULL)
			sprintf(&line[strlen(line)], " [xpath unavailable]");
		else {
			sprintf(&line[strlen(line)], " %s", path);
			free(path);
		}
	}

	currentResultPosition = start + 1;
	return true;
} // end of getNextLine()


bool GCLQuery::getStatus(int *code, char *description) {
	if (resultList != NULL)
		if (!finished)
			return false;
	if (syntaxErrorDetected) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
	}
	if (resultList == NULL) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
	}
	else {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	return true;
} // end of getStatus(int*, char*)


void GCLQuery::setResultList(ExtentList *list) {
	if (resultList != NULL)
		delete resultList;
	resultList = list;
}


