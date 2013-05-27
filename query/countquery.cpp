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
 * Implementation of the CountQuery class.
 *
 * author: Stefan Buettcher
 * created: 2004-09-26
 * changed: 2009-02-01
 **/


#include <math.h>
#include <string.h>
#include <sys/types.h>
#include "countquery.h"
#include "querytokenizer.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"


/** Valid commands understood by CountQuery. **/
static const char * COMMANDS[8] = {
	"count",
	"estimate",
	"documents",
	"docs",
	"documentsContaining",
	"histogram",
	NULL
};


CountQuery::CountQuery(Index *index, const char *command, const char **modifiers,
			const char *body, int userID, int memoryLimit) {
	this->index = index;
	if (index->APPLY_SECURITY_RESTRICTIONS)
		visibleExtents = index->getVisibleExtents(userID, false);
	else
		visibleExtents = NULL;
	mustFreeVisibleExtentsInDestructor = true;
	returnSize = false;
	isHistogram = false;
	processModifiers(modifiers);

	if ((strcasecmp(command, "count") == 0) || (strcasecmp(command, "estimate") == 0)) {
		QueryTokenizer *tok = new QueryTokenizer(body);
		subQueryCount = tok->getTokenCount();
		subQueries = typed_malloc(GCLQuery*, subQueryCount + 1);
		for (int i = 0; i < subQueryCount; i++) {
			char *token = tok->getNext();
			subQueries[i] =
				new GCLQuery(index, "gcl", modifiers, token, visibleExtents, memoryLimit);
		}
		delete tok;
	} // end if @count or @estimate

	if (strcasecmp(command, "documents") == 0) {
		subQueryCount = 1;
		subQueries = typed_malloc(GCLQuery*, 2);
		subQueries[0] =
			new GCLQuery(index, "gcl", modifiers, DOC_QUERY, visibleExtents, memoryLimit);
	} // end if @documents
	
	if ((strcasecmp(command, "docs") == 0) || (strcasecmp(command, "documentsContaining") == 0)) {
		QueryTokenizer *tok = new QueryTokenizer(body);
		subQueryCount = tok->getTokenCount();
		subQueries = typed_malloc(GCLQuery*, subQueryCount + 1);
		for (int i = 0; i < subQueryCount; i++) {
			char *token = tok->getNext();
			ExtentList *toCount = NULL;
			if ((index->DOCUMENT_LEVEL_INDEXING > 0) && (GCLQuery::isSimpleTerm(token))) {
				// we can take advantage of document-level indexing here
				char *newArgument = (char*)malloc(strlen(token) + 16);
				if (token[1] == '$')
					sprintf(newArgument, "<!>%s", &token[2]);
				else
					sprintf(newArgument, "<!>%s", &token[1]);
				int len = strlen(newArgument);
				while ((newArgument[len - 1] == '"') ||
				       ((newArgument[len - 1] >= 0) && (newArgument[len - 1] <= ' ')))
					newArgument[--len] = 0;
				if (token[1] == '$')
					strcat(newArgument, "$");
				for (int i = 0; newArgument[i] != 0; i++)
					if ((newArgument[i] >= 'A') && (newArgument[i] <= 'Z'))
						newArgument[i] += 32;
				toCount = getPostings(newArgument, userID);
				if ((index->APPLY_SECURITY_RESTRICTIONS) && (userID != Index::GOD))
					if (toCount->getType() != ExtentList::TYPE_EXTENTLIST_EMPTY)
						toCount = visibleExtents->restrictList(toCount);
				free(newArgument);
			}
			if (toCount == NULL) {
				char *newArgument = (char*)malloc(strlen(token) + 32);
				sprintf(newArgument, "(%s)>(%s)", DOC_QUERY, token);
				subQueries[i] =
					new GCLQuery(index, "gcl", modifiers, newArgument, userID, memoryLimit);
				free(newArgument);
			}
			else
				subQueries[i] = new GCLQuery(index, toCount);
		} // end for (int i = 0; i < subQueryCount; i++)
		delete tok;
	} // end if @docs or @documentsContaining

	if (strcasecmp(command, "histogram") == 0) {
		subQueryCount = 1;
		subQueries = typed_malloc(GCLQuery*, 2);
		subQueries[0] =
			new GCLQuery(index, "gcl", modifiers, body, visibleExtents, memoryLimit);
		isHistogram = true;
	} // end if @histogram

	ok = false;
} // end of CountQuery(Index*, char*, char*, char*, int)


CountQuery::~CountQuery() {
	if (subQueries != NULL) {
		for (int i = 0; i < subQueryCount; i++) {
			if (subQueries[i] != NULL)
			delete subQueries[i];
		}
		free(subQueries);
		subQueries = NULL;
	}
} // end of ~CountQuery()


bool CountQuery::isValidCommand(const char *command) {
	for (int i = 0; COMMANDS[i] != NULL; i++)
		if (strcasecmp(command, COMMANDS[i]) == 0)
			return true;
	return false;
} // end of isValidCommand(char*)


bool CountQuery::parse() {
	if (subQueryCount == 0)
		ok = false;
	else if (subQueryCount == 1)
		ok = subQueries[0]->parse();
	else
		ok = true;
	if (!ok)
		finished = true;
	return ok;
} // end of parse()


bool CountQuery::getNextLine(char *line) {
	if ((!ok) || (finished))
		return false;
	finished = true;

	if (isHistogram) {
		ExtentList *result = subQueries[0]->getResult();
		offset count = 0;
		offset sumOfLengths = 0;
		double sumOfLogOfLengths = 0.0;
		static const int MAX_LENGTH = 24;
		offset start, end, where = 0;
		offset counter[MAX_LENGTH];
		memset(counter, 0, sizeof(counter));
		while (result->getFirstStartBiggerEq(where, &start, &end)) {
			count++;
			offset length = (end - start + 1);
			sumOfLengths += length;
			sumOfLogOfLengths += log(length);
			if (length <= MAX_LENGTH)
				counter[length - 1]++;
			where = start + 1;
		}
		int lineLength = 0;
		lineLength +=
			sprintf(&line[lineLength], "%lld %d %lld %.3lf\n",
					static_cast<long long>(count),
					static_cast<int>(MAX_LENGTH),
					static_cast<long long>(sumOfLengths),
					sumOfLogOfLengths);
		offset cnt = 0;
		for (int i = 0; i < MAX_LENGTH; i++) {
			cnt += counter[i];
			lineLength += sprintf(&line[lineLength], OFFSET_FORMAT "\n", cnt);
		}
		line[lineLength - 1] = 0;
		return true;
	}
	
	if (subQueryCount == 1) {
		if (returnSize) {
			offset size = subQueries[0]->getResult()->getTotalSize();
			if (returnAverage) {
				double length = subQueries[0]->getResult()->getLength();
				sprintf(line, "%.1lf", size / length);
			}
			else
				sprintf(line, OFFSET_FORMAT, size);
		}
		else
			sprintf(line, OFFSET_FORMAT,subQueries[0]->getResult()->getLength());
		return true;
	}

	for (int i = 0; i < subQueryCount; i++) {
		if (i > 0)
			line += sprintf(line, ", ");
		if (!subQueries[i]->parse())
			line += sprintf(line, "-1");
		else {
			if (returnSize) {
				offset size = subQueries[i]->getResult()->getTotalSize();
				if (returnAverage) {
					double length = subQueries[i]->getResult()->getLength();
					sprintf(line, "%.1lf", length > 0 ? size / length : 0);
				}
				else
					sprintf(line, OFFSET_FORMAT, size);
			}
			else
				sprintf(line, OFFSET_FORMAT, subQueries[i]->getResult()->getLength());
			line += strlen(line);
		}
		delete subQueries[i];
		subQueries[i] = NULL;
	}
	return true;
} // end of getNextLine(char*)


bool CountQuery::getStatus(int *code, char *description) {
	if (!finished)
		return false;
	if (!ok) {
		*code = STATUS_ERROR;
		if (subQueryCount <= 0)
			strcpy(description, "Illegal number of arguments.");
		else
			strcpy(description, "Syntax error.");
	}
	else {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	return true;
} // end of getStatus(int*, char*)


void CountQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
	returnSize = getModifierBool(modifiers, "size", false);
	returnAverage = getModifierBool(modifiers, "avg", false);
	bool returnAvgSize = getModifierBool(modifiers, "avgSize", false);
	if (returnAvgSize)
		returnSize = returnAverage = true;
} // end of processModifiers(char**)



