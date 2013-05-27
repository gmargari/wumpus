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
 * Implementation of the MiscQuery class.
 *
 * author: Stefan Buettcher
 * created: 2004-09-28
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <string.h>
#include "miscquery.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"
#include "../stemming/stemmer.h"


static inline void takesNoArgumentsError(char *resultLine, const char *command) {
	sprintf(resultLine, "Syntax error. @%s does not take any arguments.", command);
}


MiscQuery::MiscQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->index = index;
	resultLine = (char*)malloc(MAX_RESULT_LENGTH);
	visibleExtents = index->getVisibleExtents(userID, false);
	mustFreeVisibleExtentsInDestructor = true;
	processModifiers(modifiers);

	if (strcasecmp(command, "size") == 0) {
		// @size asks for number of tokens in the corpus
		if (body[0] != 0) {
			takesNoArgumentsError(resultLine, command);
			ok = false;
			return;
		}
		if (visibleExtents == NULL)
			printOffset(0, resultLine);
		else {
			ExtentList *list = visibleExtents->getExtentList();
			offset totalSize = list->getTotalSize();
			delete list;
			printOffset(totalSize, resultLine);
		}
		ok = true;
	}
	else if (strcasecmp(command, "stem") == 0) {
		// @stem stems the given string
		char *string =
			strcpy((char*)malloc(strlen(body) * 2 + 4), body);
		Stemmer::stem(string, LANGUAGE_ENGLISH, false);
		if (string[0] == 0)
			strcpy(resultLine, "[unstemmable]");
		else
			strcpy(resultLine, string);
		free(string);
		ok = true;
	}
	else if (strcasecmp(command, "files") == 0) {
		if (body[0] != 0) {
			takesNoArgumentsError(resultLine, command);
			ok = false;
			return;
		}
		ExtentList *list = visibleExtents->getExtentList();
		printOffset(list->getLength(), resultLine);
		delete list;
		ok = true;
	}
	else if (strcasecmp(command, "about") == 0) {
		if (body[0] != 0) {
			takesNoArgumentsError(resultLine, command);
			ok = false;
			return;
		}
		char minusLine[256], wumpusLine[256], formatString[32];
		int lineLen = sprintf(wumpusLine,
				"| Wumpus Search Engine [" WUMPUS_VERSION "] - Copyright (c) 2011 by Stefan Buettcher. |");
		sprintf(formatString, "%%-%ds|\n", lineLen - 1);
		minusLine[0] = '+';
		for (int i = 1; i < lineLen - 1; i++)
			minusLine[i] = '-';
		minusLine[lineLen - 1] = '+';
		minusLine[lineLen] = 0;
		int len = 0;
		len += sprintf(&resultLine[len], "%s\n", minusLine);
		len += sprintf(&resultLine[len], "%s\n", wumpusLine);
		len += sprintf(&resultLine[len], formatString, "|");
		len += sprintf(&resultLine[len], formatString,
				"| This is free software according to the GNU General Public License (GPL).");
		len += sprintf(&resultLine[len], formatString,
				"|  - http://www.gnu.org/philosophy/free-sw.html");
		len += sprintf(&resultLine[len], formatString,
				"|  - http://www.gnu.org/copyleft/gpl.html");
		len += sprintf(&resultLine[len], "%s", minusLine);
		ok = true;
	}
	else if (strcasecmp(command, "fileinfo") == 0) {
		bool syntax = isNumber(body);
		offset value;
		if (sscanf(body, OFFSET_FORMAT, &value) != 1)
			syntax = false;
		if ((syntax) && (value >= 0)) {
			char *fileName = visibleExtents->getFileNameForOffset(value);
			if (fileName == NULL) {
				strcpy(resultLine, "File not found.");
				ok = false;
			}
			else {
				int documentType = index->getDocumentType(fileName);
				char *fileType = FilteredInputStream::documentTypeToString(documentType);
				sprintf(resultLine, "%s %s", fileType, fileName);
				free(fileType);
				free(fileName);
				ok = true;
			}
		}
		else {
			strcpy(resultLine, "Syntax error.");
			ok = false;
		}
	}
	else if (strcasecmp(command, "summary") == 0) {
		if (body[0] != 0) {
			ok = false;
			return;
		}
		resultLine[0] = 0;
		index->getIndexSummary(resultLine);
		if (resultLine[0] != 0)
			if (resultLine[strlen(resultLine) - 1] == '\n')
				resultLine[strlen(resultLine) - 1] = 0;
		ok = true;
	}
	else if (strcasecmp(command, "filestats") == 0) {
		if (body[0] != 0) {
			takesNoArgumentsError(resultLine, command);
			ok = false;
			return;
		}
		ExtentList *files = getPostings("<file!>", Index::GOD);
		ExtentList *visible = visibleExtents->restrictList(files);
		offset s, e, p = 0;
		int table[256];
		memset(table, 0, sizeof(table));
		while (visible->getFirstStartBiggerEq(p, &s, &e)) {
			int documentType = visibleExtents->getDocumentTypeForOffset(s);
			if (documentType >= 0)
				table[documentType]++;
			p = s + 1;
		}
		delete visible;
		int outLen = 0;
		resultLine[outLen] = 0;
		for (int i = 0; i < 256; i++)
			if (table[i] > 0) {
				char *typeString = FilteredInputStream::documentTypeToString(i);
				outLen += sprintf(&resultLine[outLen], "%s: %d\n", typeString, table[i]);
				free(typeString);
			}
		if (outLen > 0)
			resultLine[outLen - 1] = 0;
		ok = true;
	}
	else if (strcasecmp(command, "dictionarysize") == 0) {
		// @dictionarysize asks for the number of terms in the Lexicon;
		// note that only superuser and index owner may send this query
		if (body[0] != 0) {
			takesNoArgumentsError(resultLine, command);
			ok = false;
			return;
		}
		if ((userID == index->SUPERUSER) || (userID == index->getOwner())) {
			offset lower, upper;
			index->getDictionarySize(&lower, &upper);
			if (lower == upper)
				sprintf(resultLine, "#terms = " OFFSET_FORMAT, lower);
			else
				sprintf(resultLine, OFFSET_FORMAT " <= #terms <= " OFFSET_FORMAT, lower, upper);
			ok = true;
		}
		else {
			sprintf(resultLine, "Permission denied.");
			ok = false;
		}
	}
	else if (strcasecmp(command, "addannotation") == 0) {
		int pos = 0;
		offset indexOffset = 0;
		while ((body[pos] >= '0') && (body[pos] <= '9')) {
			indexOffset = indexOffset * 10 + (body[pos] - '0');
			pos++;
		}
		if ((body[pos] > ' ') || (body[pos] < 0)) {
			sprintf(resultLine, "Illegal index position.");
			ok = false;
		}
		else {
			while ((body[pos] > 0) && (body[pos] <= ' '))
				pos++;
			if (body[pos] == 0) {
				sprintf(resultLine, "No annotation specified.");
				ok = false;
			}
			else if (!mayAccessIndexExtent(indexOffset, indexOffset)) {
				sprintf(resultLine, "Permission denied.");
				ok = false;
			}
			else {
				index->addAnnotation(indexOffset, &body[pos]);
				sprintf(resultLine, "# Annotation added.");
				ok = true;
			}
		}
	}
	else if (strcasecmp(command, "getannotation") == 0) {
		int pos = 0;
		offset indexOffset = 0;
		while ((body[pos] >= '0') && (body[pos] <= '9')) {
			indexOffset = indexOffset * 10 + (body[pos] - '0');
			pos++;
		}
		if ((body[pos] > ' ') || (body[pos] < 0)) {
			sprintf(resultLine, "Illegal index position.");
			ok = false;
		}
		else {
			while ((body[pos] > 0) && (body[pos] <= ' '))
				pos++;
			if (body[pos] != 0) {
				sprintf(resultLine, "Illegal number of arguments.");
				ok = false;
			}
			else if (!mayAccessIndexExtent(indexOffset, indexOffset)) {
				sprintf(resultLine, "Permission denied.");
				ok = false;
			}
			else {
				index->getAnnotation(indexOffset, resultLine);
				ok = true;
			}
		}
	}
	else if (strcasecmp(command, "removeannotation") == 0) {
		int pos = 0;
		offset indexOffset = 0;
		while ((body[pos] >= '0') && (body[pos] <= '9')) {
			indexOffset = indexOffset * 10 + (body[pos] - '0');
			pos++;
		}
		if ((body[pos] > ' ') || (body[pos] < 0)) {
			sprintf(resultLine, "Illegal index position.");
			ok = false;
		}
		else {
			while ((body[pos] > 0) && (body[pos] <= ' '))
				pos++;
			if (body[pos] != 0) {
				sprintf(resultLine, "Illegal number of arguments.");
				ok = false;
			}
			else if (!mayAccessIndexExtent(indexOffset, indexOffset)) {
				sprintf(resultLine, "Permission denied.");
				ok = false;
			}
			else {
				index->removeAnnotation(indexOffset);
				sprintf(resultLine, "# Annotation removed.");
				ok = true;
			}
		}
	}
	else if (strcasecmp(command, "system") == 0) {
		if ((userID == index->SUPERUSER) || (userID == index->getOwner())) {
			ok = (system(body) >= 0);
			resultLine[0] = 0;
		}
		else {
			strcpy(resultLine, "Permission denied.");
			ok = false;
		}
	}
	else {
		strcpy(resultLine, "Illegal command.");
		ok = false;
	}
} // end of MiscQuery(Index*, char*, char*, char*, int)


MiscQuery::~MiscQuery() {
	if (resultLine != NULL) {
		free(resultLine);
		resultLine = NULL;
	}
} // end of ~MiscQuery()


bool MiscQuery::parse() {
	return true;
} // end of parse()


bool MiscQuery::getNextLine(char *line) {
	if (ok) {
		if (finished)
			return false;
		strcpy(line, resultLine);
		finished = true;
		return true;
	}
	return false;
} // end of getNextLine(char*)


bool MiscQuery::getStatus(int *code, char *description) {
	if (ok) {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	else {
		*code = STATUS_ERROR;
		strcpy(description, resultLine);
	}
	return true;
} // end of getStatus(int*, char*)


int MiscQuery::getType() {
	return QUERY_TYPE_MISC;
}


void MiscQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
}



