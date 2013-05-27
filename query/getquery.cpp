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
 * Implementation of the GetQuery class.
 *
 * author: Stefan Buettcher
 * created: 2004-10-03
 * changed: 2009-02-01
 **/


#include "getquery.h"
#include "../misc/all.h"
#include <assert.h>
#include <string.h>


void GetQuery::initialize(Index *index, const char *command, const char **modifiers,
		const char *body, VisibleExtents *visibleExtents) {
	this->index = index;
	this->visibleExtents = visibleExtents;
	filtered = false;
	ok = false;
	permissionDenied = false;
	fileError = false;
	fileName = NULL;
	tppSpeedup = NULL;

	processModifiers(modifiers);
	queryString = strcpy((char*)malloc(strlen(body) + 4), body);
} // end of initialize(...)


GetQuery::GetQuery(Index *index, const char *command, const char **modifiers, const char *body,
		VisibleExtents *visibleExtents, int memoryLimit) {
	initialize(index, command, modifiers, body, visibleExtents);
	mustFreeVisibleExtentsInDestructor = false;
} // end of GetQuery(...)


GetQuery::GetQuery(Index *index, const char *command, const char **modifiers, const char *body,
		uid_t userID, int memoryLimit) {
	this->userID = userID;
	VisibleExtents *visibleExtents = index->getVisibleExtents(userID, false);
	initialize(index, command, modifiers, body, visibleExtents);
	mustFreeVisibleExtentsInDestructor = true;
} // end of GetQuery(Index*, char*, char**, char*, uid_t, int)


GetQuery::GetQuery(Index *index, offset start, offset end, bool filtered) {
	char body[64];
	sprintf(body, OFFSET_FORMAT " " OFFSET_FORMAT, start, end);
	this->userID = Index::GOD;
	VisibleExtents *visibleExtents = index->getVisibleExtents(Index::GOD, false);
	initialize(index, "get", EMPTY_MODIFIERS, body, visibleExtents);
	this->filtered = filtered;
	mustFreeVisibleExtentsInDestructor = true;
} // end of GetQuery(Index*, offset, offset)


GetQuery::~GetQuery() {
	if (fileName != NULL) {
		free(fileName);
		fileName = NULL;
	}
	if (tppSpeedup != NULL) {
		free(tppSpeedup);
		tppSpeedup = NULL;
	}
} // end of ~GetQuery()


bool GetQuery::isValidCommand(const char *command) {
	if (strcasecmp(command, "get") == 0)
		return true;
	else
		return false;
} // end of isValidCommand(char*)


bool GetQuery::parse() {
	if (sscanf(queryString, OFFSET_FORMAT OFFSET_FORMAT, &startOffset, &endOffset) != 2)
		return (ok = false);
	if (endOffset < startOffset)
		return (ok = false);
	if (visibleExtents == NULL)
		return (ok = true);

	// check if the stuff may be read by the user who submitted the query
	ExtentList *list = visibleExtents->getExtentList();
	offset start, end;
	if (!list->getLastStartSmallerEq(startOffset, &start, &end))
		start = end = -1;
	else if (end < endOffset)
		start = end = -1;
	delete list;

	// obtain file name and document type
	if (start < 0) {
		permissionDenied = true;
		return (ok = false);
	}
	fileName = visibleExtents->getFileNameForOffset(start);
	if (fileName == NULL) {
		permissionDenied = true;
		return (ok = false);
	}
	documentType = index->getDocumentType(fileName);
	if (documentType == FilteredInputStream::DOCUMENT_TYPE_UNKNOWN) {
		permissionDenied = true;
		return (ok = false);
	}

	// get speedup information from IndexToText
	offset speedupIndexPosition;
	off_t speedupFilePosition;
	if (index->getLastIndexToTextSmallerEq(startOffset, &speedupIndexPosition, &speedupFilePosition)) {
		if ((speedupIndexPosition <= startOffset) && (speedupIndexPosition >= start)) {
			tppSpeedup = typed_malloc(TokenPositionPair, 2);
			tppSpeedup[0].sequenceNumber = speedupIndexPosition - start;
			tppSpeedup[0].filePosition = speedupFilePosition;
			tppSpeedup[1].sequenceNumber = 0;
			tppSpeedup[1].filePosition = 0;
		}
	}

	// adjust start and end offset to be consistent with the input file
	startOffset -= start;
	endOffset -= start;

	return (ok = true);
} // end of parse()


bool GetQuery::getNextLine(char *line) {
	if ((!ok) || (finished))
		return false;
	finished = true;

	FilteredInputStream *inputStream =
			FilteredInputStream::getInputStream(fileName, documentType,
					(index == NULL ? NULL : index->getDocumentCache(fileName)));

	if (inputStream->getFileHandle() < 0) {
		strcpy(line, "(text unavailable)");
		ok = false;
		fileError = true;
		delete inputStream;
		return true;
	}

	// decrease the input stream's read buffer size so as to not read too much
	// stuff we don't actually need for this query
	inputStream->useSmallBuffer();

	char *result;
	int length, tokens;

	// When returning the result of the query, we have to be aware of "@" at
	// the start of a line because that can confuse the message recipient.
	// The current solution is to put an additional "@" in front of every line
	// starting with an "@" symbol.

	if (filtered) {
		result = inputStream->getFilteredRange(
				startOffset, endOffset, tppSpeedup, &length, &tokens);
		if (result[0] == '@') {
			line[0] = '@';
			line = &line[1];
		}
		if (strlen(result) >= MAX_RESPONSELINE_LENGTH - 2)
			result[MAX_RESPONSELINE_LENGTH - 2] = 0;
		strcpy(line, result);
		free(result);
	}
	else {
		result = inputStream->getRange(
				startOffset, endOffset, tppSpeedup, &length, &tokens);
		char *allocPtr = result;
		int writtenSoFar = 0;
		if (result[0] == '@') {
			line[0] = ' ';
			writtenSoFar = 1;
		}
		char *problemZone = strstr(result, "\n@");
		while (problemZone != NULL) {
			problemZone[0] = 0;
			if (writtenSoFar + strlen(result) + 4 >= MAX_RESPONSELINE_LENGTH)
				break;
			writtenSoFar += sprintf(&line[writtenSoFar], "%s", result);
			if (writtenSoFar + 6 >= MAX_RESPONSELINE_LENGTH)
				break;
			writtenSoFar += sprintf(&line[writtenSoFar], "\n@@");
			result = &problemZone[2];
			problemZone = strstr(result, "\n@");
		}
		if (writtenSoFar + strlen(result) + 4 >= MAX_RESPONSELINE_LENGTH) {
			int whereToCut = MAX_RESPONSELINE_LENGTH - writtenSoFar - 4;
			if (whereToCut < 0)
				whereToCut = 0;
			result[whereToCut] = 0;
		}
		writtenSoFar += sprintf(&line[writtenSoFar], "%s", result);
		free(allocPtr);
	}
	delete inputStream;

	return true;
} // end of getNextLine(char*)


bool GetQuery::getStatus(int *code, char *description) {
	if (ok) {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	else if (visibleExtents == NULL) {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	else if (fileError) {
		*code = STATUS_ERROR;
		strcpy(description, "Unable to open file.");
	}
	else if (permissionDenied) {
		*code = STATUS_ERROR;
		strcpy(description, "Permission denied.");
	}
	else {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
	}
	return true;
} // end of getStatus(int*, char*)


int GetQuery::getType() {
	return QUERY_TYPE_GET;
}


void GetQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
	filtered = getModifierBool(modifiers, "filtered", false);
}


