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
 * Implementation of the multi-purpose Query interface.
 *
 * author: Stefan Buettcher
 * created: 2004-09-26
 * changed: 2009-02-01
 **/


#include <string.h>
#include <sys/times.h>
#include <map>
#include <string>
#include <vector>

// Below, you find a list of header files for all query types supported by
// Wumpus. It is absolutely crucial that *every* query header file is included
// here, for otherwise the linker will not execute the static initialization
// code in the object files, and the query commands will not be registered with
// the query dispatcher.
#define __IN_QUERY_CPP__
#include "query.h"
#include "bm25query.h"
#include "bm25f_query.h"
#include "cdrquery.h"
#include "countquery.h"
#include "desktopquery.h"
#include "divergence_query.h"
#include "experimental_query.h"
#include "gclquery.h"
#include "getquery.h"
#include "helpquery.h"
#include "languagemodel_query.h"
#include "miscquery.h"
#include "qapquery.h"
#include "rankedquery.h"
#include "synonymquery.h"
#include "updatequery.h"
#include "vectorspace_query.h"
#include "xpathquery.h"
#include "../terabyte/terabyte_query.h"
#include "../terabyte/chapter6.h"

#include "../indexcache/docidcache.h"
#include "../misc/all.h"

using namespace std;


const char * DOC_QUERY = "\"<doc>\"..\"</doc>\"";
const char * DOCNO_QUERY = "\"<docno>\"..\"</docno>\"";
const char * FILE_QUERY = "\"<file!>\"..\"</file!>\"";
const char * EMPTY_MODIFIERS[1] = { NULL };

static const char * LOG_ID = "Query";


void Query::initialize() {
	// record start time of query so that we can obtain performance statistics
	startTime = currentTimeMillis();
	times(&cpuStartTime);

	actualQuery = NULL;
	visibleExtents = NULL;
	mustFreeVisibleExtentsInDestructor = false;
	I_AM_THE_REAL_QUERY = false;
	memoryLimit = DEFAULT_MEMORY_LIMIT;
	queryString = NULL;
	queryTokenizer = NULL;
	finished = false;
	additionalQuery = NULL;
	addGet = false;
	onlyFromDisk = false;
	onlyFromMemory = false;
	verboseText = NULL;
} // end of initialize()


Query::Query() {
	initialize();
}


Query::Query(Index *index, const char *queryString, int userID) {
	initialize();
	this->index = index;
	this->userID = userID;
	this->queryString = replaceMacros(queryString);
	I_AM_THE_REAL_QUERY = true;
	syntaxErrorDetected = false;
	actualQuery = NULL;
	visibleExtents = NULL;
	mustFreeVisibleExtentsInDestructor = false;

	queryString = this->queryString;

	int memoryLimit;
	getConfigurationInt("MAX_QUERY_SPACE", &memoryLimit, DEFAULT_MEMORY_LIMIT);
	if (!index->APPLY_SECURITY_RESTRICTIONS)
		this->userID = Index::GOD;

	// register with the Index; return value < 0 means: Index is shutting down;
	// let's do the same...
	indexUserID = index->registerForUse();
	if (indexUserID < 0) {
		log(LOG_ERROR, LOG_ID, "Query registration failed.");
		this->index = NULL;
		return;
	}

	// split the query into its 3 different parts: command, modifiers, arguments;
	// start with the command part
	while (isWhiteSpace(*queryString))
		queryString++;
	if (queryString[0] == 0)
		return;
	if (strlen(queryString) > MAX_QUERY_LENGTH)
		return;

	if (queryString[0] != '@') {
		// if there is no '@' in front of the query string, it has to be a GCL query
		actualQuery =
			new GCLQuery(index, "gcl", EMPTY_MODIFIERS, queryString, userID, memoryLimit);
		return;
	}

	// skip '@' and find end of command string
	queryString++;
	if (queryString[0] == 0)
		return;
	char *command = const_cast<char*>(queryString);
	while (*queryString != 0) {
		if ((isWhiteSpace(*queryString)) || (*queryString == '['))
			break;
		queryString++;
	}

	long commandLen = ((long)queryString) - ((long)command);
	command = strncpy((char*)malloc(commandLen + 1), command, commandLen);
	command[commandLen] = 0;

	// command extracted; proceed with modifiers
	while (isWhiteSpace(*queryString))
		queryString++;

	// create a NULL-terminated list of modifier strings, with the '[' and
	// ']' characters removed; if the number of modifiers exceeds MAX_MODIFIER_COUNT,
	// stop parsing and report an error
	char **modifiers = typed_malloc(char*, MAX_MODIFIER_COUNT + 1);
	int modifierCount = 0;
	while (*queryString == '[') {
		if (modifierCount >= MAX_MODIFIER_COUNT) {
			for (int i = 0; i < modifierCount; i++)
				free(modifiers[i]);
			free(modifiers);
			free(command);
			syntaxErrorDetected = true;
			return;
		}
		char *modifier = const_cast<char*>(++queryString);
		bool inQuotes = false;
		while (*queryString != 0) {
			if (*queryString == '"')
				inQuotes = !inQuotes;
			else if ((!inQuotes) && (*queryString == ']')) {
				queryString++;
				break;
			}
			queryString++;
		}
		int modifierLen = ((long)queryString) - ((long)modifier);
		while (modifierLen > 0) {
			if ((modifier[modifierLen - 1] == '[') || (modifier[modifierLen - 1] == ']'))
				modifierLen--;
			else
				break;
		}
		modifier = strncpy((char*)malloc(modifierLen + 1), modifier, modifierLen);
		modifier[modifierLen] = 0;
		modifiers[modifierCount++] = modifier;
	} // end while (*queryString == '[')
	modifiers[modifierCount] = NULL;

	// modifiers extracted; the rest is the query body
	while (isWhiteSpace(*queryString))
		queryString++;
	char *body = duplicateString(queryString);

	QueryFactoryMethod factoryMethod = getQueryFactoryMethod(command);
	if (factoryMethod != NULL)
		actualQuery = factoryMethod(index, command, (const char**)modifiers, body, userID, memoryLimit);
	else if (UpdateQuery::isValidCommand(command)) {
		// if we have an update query, we have to deregister immediately, since
		// otherwise we can cause a deadlock inside the index; deregistering is
		// not dangerous because we don't access any posting lists anyway
		index->deregister(indexUserID);
		indexUserID = -1;
		actualQuery = new UpdateQuery(index, command, (const char**)modifiers, body, userID, memoryLimit);
	}
	else if (XPathQuery::isValidCommand(command))
		actualQuery = new XPathQuery(index, command, (const char**)modifiers, body, userID, memoryLimit);
	else if (GCLQuery::isValidCommand(command))
		actualQuery =
			new GCLQuery(index, command, (const char**)(modifiers), body, userID, memoryLimit);

	// free all temporary storage space
	if (command != NULL)
		free(command);
	if (modifiers != NULL) {
		for (int i = 0; modifiers[i] != NULL; i++)
			free(modifiers[i]);
		free(modifiers);
	}
	if (body != NULL)
		free(body);
} // end of Query(Index, char*)


Query::~Query() {
	if (actualQuery != NULL) {
		delete actualQuery;
		actualQuery = NULL;
	}
	if (queryString != NULL) {
		free(queryString);
		queryString = NULL;
	}
	if (queryTokenizer != NULL) {
		free(queryTokenizer);
		queryTokenizer = NULL;
	}
	if ((visibleExtents != NULL) && (mustFreeVisibleExtentsInDestructor)) {
		delete visibleExtents;
		visibleExtents = NULL;
	}
	if (additionalQuery != NULL) {
		delete additionalQuery;
		additionalQuery = NULL;
	}
	if (verboseText != NULL) {
		free(verboseText);
		verboseText = NULL;
	}
	if ((index != NULL) && (I_AM_THE_REAL_QUERY) && (indexUserID >= 0))
		index->deregister(indexUserID);
} // end of ~Query()


void Query::processModifiers(const char **modifiers) {
	verbose = getModifierBool(modifiers, "verbose", false);
	printFileName = getModifierBool(modifiers, "filename", false);
	printPageNumber = getModifierBool(modifiers, "page", false);
	printPageNumber = getModifierBool(modifiers, "pageno", printPageNumber);
	printDocumentID = getModifierBool(modifiers, "docid", false);
	queryTokenizer = getModifierString(modifiers, "tokenizer", DEFAULT_QUERY_TOKENIZER);
	onlyFromDisk = getModifierBool(modifiers, "disk_only", false);
	onlyFromMemory = getModifierBool(modifiers, "mem_only", false);
	useCache = getModifierBool(modifiers, "usecache", true);
	useCache = !getModifierBool(modifiers, "nocache", false);
	count = DEFAULT_COUNT;
	for (int i = 0; modifiers[i] != NULL; i++) {
		if ((strlen(modifiers[i]) > 0) && (isNumber(modifiers[i]))) {
			int value;
			if (sscanf(modifiers[i], "%d", &value) == 1) {
				if (value < 1)
					value = 1;
				if (value > MAX_COUNT)
					value = MAX_COUNT;
				count = value;
			}
		}
	}
	count = getModifierInt(modifiers, "count", count);
	if (count < 1)
		count = 0;
	if (count > MAX_COUNT)
		count = MAX_COUNT;
} // end of processModifiers(char**)


bool Query::getModifierBool(const char **modifiers, const char *name, bool defaultValue) {
	if (modifiers == NULL)
		return defaultValue;
	int nameLen = strlen(name);
	for (int i = 0; modifiers[i] != NULL; i++) {
		if (strlen(modifiers[i]) < nameLen)
			continue;
		if (strncasecmp(modifiers[i], name, nameLen) == 0) {
			if (modifiers[i][nameLen] == 0)
				return true;
			else if (modifiers[i][nameLen] == '=') {
				if (strcasecmp(&modifiers[i][nameLen + 1], "true") == 0)
					return true;
				if (strcasecmp(&modifiers[i][nameLen + 1], "false") == 0)
					return false;
			}
		}
	}
	return defaultValue;
} // end of getModifierBool(char**, char*, bool)


int Query::getModifierInt(const char **modifiers, const char *name, int defaultValue) {
	if (modifiers == NULL)
		return defaultValue;
	int value;
	int nameLen = strlen(name);
	for (int i = 0; modifiers[i] != NULL; i++) {
		if (strlen(modifiers[i]) < nameLen + 2)
			continue;
		if (strncasecmp(modifiers[i], name, nameLen) == 0)
			if (modifiers[i][nameLen] == '=')
				if (isNumber(&modifiers[i][nameLen + 1]))
					if (sscanf(&modifiers[i][nameLen + 1], "%d", &value) == 1)
						return value;
	}
	return defaultValue;
} // end of getModifierInt(char**, char*, int)


double Query::getModifierDouble(const char **modifiers, const char *name, double defaultValue) {
	if (modifiers == NULL)
		return defaultValue;
	double value;
	unsigned int nameLen = strlen(name);
	for (int i = 0; modifiers[i] != NULL; i++) {
		if (strlen(modifiers[i]) < nameLen + 2)
			continue;
		if (strncasecmp(modifiers[i], name, nameLen) == 0)
			if (modifiers[i][nameLen] == '=')
				if (sscanf(&modifiers[i][nameLen + 1], "%lf", &value) == 1)
					return value;
	}
	return defaultValue;
} // end of getModifierDouble(char**, char*, double)


char * Query::getModifierString(
		const char **modifiers, const char *name, const char *defaultValue) {
	if (modifiers == NULL)
		return duplicateString(defaultValue);
	int nameLen = strlen(name);
	for (int i = 0; modifiers[i] != NULL; i++) {
		if (strlen(modifiers[i]) < nameLen + 2)
			continue;
		if (strncasecmp(modifiers[i], name, nameLen) == 0)
			if (modifiers[i][nameLen] == '=')
				return duplicateString(&modifiers[i][nameLen + 1]);
	}
	return duplicateString(defaultValue);
} // end of getModifierString(char**, char*, char*)


bool Query::mayAccessIndexExtent(offset start, offset end) {
	if (visibleExtents == NULL)
		return true;
	offset s, e;
	ExtentList *list = visibleExtents->getExtentList();
	bool found = list->getLastStartSmallerEq(start, &s, &e);
	delete list;
	return (found) && (e >= end);
} // end of mayAccessIndexExtent(start, end)


bool Query::parse() {
	if ((actualQuery == NULL) || (syntaxErrorDetected)) {
		syntaxErrorDetected = true;
		return false;
	}
	return actualQuery->parse();
} // end of parse()


bool Query::getNextLine(char *line) {
	if (syntaxErrorDetected)
		return false;
	if (verboseText != NULL) {
		strcpy(line, verboseText);
		free(verboseText);
		verboseText = NULL;
		return true;
	}
	else
		return actualQuery->getNextLine(line);
} // end of getNextLine(char*)


void Query::addVerboseString(const char *key, const char *value) {
	if (verboseText == NULL)
		verboseText = duplicateString("");
	int len = strlen(verboseText);
	verboseText =
		(char*)realloc(verboseText, len + (key == NULL ? 0 : strlen(key)) + strlen(value) + 8);
	if (len > 0)
		len += sprintf(&verboseText[len], "\n");
	if (key == NULL)
		sprintf(&verboseText[len], "# %s", value);
	else
		sprintf(&verboseText[len], "# %s: %s", key, value);
} // end of addVerboseString(char*, char*)


void Query::addVerboseDouble(const char *key, double value) {
	if (verboseText == NULL)
		verboseText = duplicateString("");
	int len = strlen(verboseText);
	verboseText =
		(char*)realloc(verboseText, len + (key == NULL ? 0 : strlen(key)) + 32);
	if (len > 0)
		len += sprintf(&verboseText[len], "\n");
	if (key == NULL)
		sprintf(&verboseText[len], "# %.4lf", value);
	else
		sprintf(&verboseText[len], "# %s: %.4lf", key, value);
} // end of addVerboseDouble(char*, double)


bool Query::getStatus(int *code, char *description) {
	if (I_AM_THE_REAL_QUERY) {
		bool result;
		if (index == NULL) {
			*code = STATUS_ERROR;
			strcpy(description, "Unable to process query: Index has entered shutdown sequence.");
			result = true;
		}
		else if (actualQuery == NULL) {
			*code = STATUS_ERROR;
			strcpy(description, "Invalid command.");
			result = true;
		}
		else if (syntaxErrorDetected) {
			*code = STATUS_ERROR;
			strcpy(description, "Syntax error.");
			result = true;
		}
		else {
			description[0] = 0;
			result = actualQuery->getStatus(code, description);
		}

		// add time elapsed to status line
		bool reportCPU = false;
		getConfigurationBool("REPORT_CPU_TIME", &reportCPU, false);
		if (reportCPU) {
			struct tms cpuEndTime;
			times(&cpuEndTime);
			double cpuElapsed = (double)
				((cpuEndTime.tms_utime + cpuEndTime.tms_stime) - (cpuStartTime.tms_utime + cpuStartTime.tms_stime));
			long ticksPerSecond = sysconf(_SC_CLK_TCK);
			sprintf(&description[strlen(description)], " (%.0lf ms CPU)", cpuElapsed * 1E3 / ticksPerSecond);
		}
		else {
			int timeElapsed = currentTimeMillis() - startTime;
			if (timeElapsed < 0)
				timeElapsed += MILLISECONDS_PER_DAY;
			sprintf(&description[strlen(description)], " (%d ms)", timeElapsed);
		}

		return result;
	}
	else if (!finished)
		return false;
	else if (syntaxErrorDetected) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
		return true;
	}
	else {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
		return true;
	}
} // end of getStatus(int*, char*)


void Query::addFileNameToResultLine(char *line, offset posInFile) {
	if (visibleExtents == NULL) {
		sprintf(&line[strlen(line)], " [file_not_found]");
		return;
	}
	char *fileName = visibleExtents->getFileNameForOffset(posInFile);
	if (fileName == NULL) {
		sprintf(&line[strlen(line)], " [file_not_found]");
		return;
	}
	if (strlen(fileName) >= 128)
		sprintf(&line[strlen(line)], " [filename_too_long]");
	else {
		StringTokenizer *tok = new StringTokenizer(fileName, " \t");
		int len = strlen(line);
		line[len++] = ' ';
		while (tok->hasNext()) {
			char *part = tok->getNext();
			sprintf(&line[len], "%s", part);
			len += strlen(part);
			if (tok->hasNext()) {
				sprintf(&line[len], "%%20");
				len += 3;
			}
		}
		delete tok;
	}
	free(fileName);
} // end of addFileNameToResultLine(Index*, char*, offset)


void Query::addPageNumberToResultLine(char *line, offset startPos, offset endPos) {
	if ((visibleExtents == NULL) || (index == NULL)) {
		strcat(line, " [unknown_page]");
		return;
	}

	// obtain start and end offset of file containing the given range
	ExtentList *list = visibleExtents->getExtentList();
	offset start, end;
	if (!list->getLastStartSmallerEq(startPos, &start, &end))
		start = end = -1;
	else if (end < endPos)
		start = end = -1;
	delete list;
	if (start < 0) {
		strcat(line, " [unknown_page]");
		return;
	}

	ExtentList *pages = getPostings("<newpage/>", Index::GOD);
	int newPageTagsBefore, newPageTagsWithin;
	if (startPos <= start)
		newPageTagsBefore = 0;
	else
		newPageTagsBefore = pages->getCount(start, startPos - 1);
	newPageTagsWithin = pages->getCount(startPos, endPos);
	delete pages;

	sprintf(&line[strlen(line)], " %d", newPageTagsBefore + 1);
	if (newPageTagsWithin > 0)
		sprintf(&line[strlen(line)], " -%d", newPageTagsBefore + 1 + newPageTagsWithin);
} // end of addPageNumberToResultLine(char*, offset, offset)


void Query::addAnnotationToResultLine(char *line, offset startPos) {
	char annotation[MAX_ANNOTATION_LENGTH * 2];
	index->getAnnotation(startPos, annotation);
	sprintf(&line[strlen(line)], " \"%s\"", annotation);
} // end of addAnnotationToResultLine(char*, offset)


void Query::getDocIdForOffset(char* result, offset start, offset end, bool isDocStart) {
	if (!isDocStart) {
		offset s, e;
		GCLQuery *q =
			new GCLQuery(index, "gcl", EMPTY_MODIFIERS, DOC_QUERY, visibleExtents, -1);
		q->parse();
		if (q->getResult()->getLastStartSmallerEq(start, &s, &e)) {
			if (e >= end)
				start = s;
			else
				start = -1;
		}
		else
			start = -1;
		delete q;
	}
	strcpy(result, "n/a");
	if (start >= 0) {
		DocIdCache *dCache = index->documentIDs;
		if (dCache != NULL) {
			char *docID = dCache->getDocumentID(start);
			if (docID != NULL) {
				char *docid = duplicateAndTrim(docID);
				strcpy(result, docid);
				free(docid);
				free(docID);
			}
		}
	}
} // end of getDocIdForOffset(char*, offset, offset, bool)


void Query::addAdditionalStuffToResultLine(char *line, offset startPos, offset endPos) {
	offset addFrom, addTo = MAX_OFFSET;
	ExtentList *addQ = additionalQuery->getResult();
	addQ->getFirstStartBiggerEq(startPos, &addFrom, &addTo);
	if (!addGet) {
		if (addTo > endPos)
			sprintf(&line[strlen(line)], " -1 -1");
		else
			sprintf(&line[strlen(line)], " " OFFSET_FORMAT " " OFFSET_FORMAT, addFrom, addTo);
	}
	else if (addTo > endPos)
		strcat(line, " \"n/a\"");
	else {
		char arguments[32];
		sprintf(arguments, OFFSET_FORMAT " " OFFSET_FORMAT, addFrom, addTo);
		GetQuery *gq =
			new GetQuery(index, "get", EMPTY_MODIFIERS, arguments, visibleExtents, -1);
		gq->parse();
		char *result = (char*)malloc(FilteredInputStream::MAX_FILTERED_RANGE_SIZE);
		if (gq->getNextLine(result)) {
			result[256] = 0;
			char *ptr = result;
			while (isWhiteSpace(*ptr))
				ptr++;
			int len = strlen(ptr);
			while ((len > 1) && (isWhiteSpace(ptr[len - 1])))
				ptr[--len] = 0;
			strcpy(result, ptr);
			for (int i = 0; result[i] != 0; i++)
				if (isWhiteSpace(result[i]))
					result[i] = ' ';
				else if (result[i] == '"')
					result[i] = '\'';
		}
		else
			result[0] = 0;
		sprintf(&line[strlen(line)], " \"%s\"", result);
		free(result);
		delete gq;
	} // end else [addGet]
} // end of addAdditionalStuffToResultLine(char*, offset, offset)


int Query::getType() {
	if (I_AM_THE_REAL_QUERY) {
		if (actualQuery == NULL)
			return QUERY_TYPE_UNKNOWN;
		else
			return actualQuery->getType();
	}
	else
		return QUERY_TYPE_UNKNOWN;
} // end of getType()


char * Query::replaceMacros(const char *query) {
	char result[MAX_QUERY_LENGTH * 2];
	int resultLen = 0;
	bool inQuotes = false;

	if (query == NULL)
		return NULL;

	while ((*query != 0) && (resultLen <= MAX_QUERY_LENGTH)) {
		if ((*query == '$') && (!inQuotes)) {
			// macro found in query string: extract macro name; take care of two
			// different cases: "$(MACRO)" and "$MACRO"
			char macro[MAX_CONFIG_KEY_LENGTH + 2];
			int macroLen = 0;
			query++;
			if (*query == '(') {
				query++;
				while ((*query != 0) && (*query != ')')) {
					if (macroLen < MAX_CONFIG_KEY_LENGTH)
						macro[macroLen++] = *query;
					query++;
				}
				if (*query == ')')
					query++;
			}
			else {
				while (((*query | 32) >= 'a') && ((*query | 32) <= 'z')) {
					if (macroLen < MAX_CONFIG_KEY_LENGTH)
						macro[macroLen++] = *query;
					query++;
				}
			}
			macro[macroLen] = 0;

			// try to retrieve macro definition from configurator
			char configKey[80], configValue[MAX_CONFIG_VALUE_LENGTH];
			sprintf(configKey, "MACRO:%s", macro);
			if (getConfigurationValue(configKey, configValue))
				resultLen += sprintf(&result[resultLen], " %s ", configValue);
			else
				resultLen += sprintf(&result[resultLen], "$(%s)", macro);
		}
		else {
			if (*query == '"')				
				inQuotes = !inQuotes;
			result[resultLen++] = *query;
			query++;
		}
	}

	result[resultLen] = 0;
	return duplicateString(result);
} // end of replaceMacros(char*)


int Query::getCount() {
	return count;
}


char * Query::getQueryString() {
	if (actualQuery != NULL)
		return actualQuery->getQueryString();
	else
		return duplicateString(queryString);
}


ExtentList * Query::getPostings(const char *term, uid_t userID) {
	if (index == NULL)
		return new ExtentList_Empty();
	else if (onlyFromDisk)
		return index->getPostings(term, userID, true, false);
	else if (onlyFromMemory)
		return index->getPostings(term, userID, false, true);
	else
		return index->getPostings(term, userID);
} // end of getPostings(char*, uid_t)



// The following stuff is for registering query types and properly executing them
// at run-time.

struct QueryTypeDescriptor {
	Query*(*factoryMethod)(Index*, const char*, const char**, const char*, uid_t, int);
	vector<string> commands;
	string summary;
	string helpText;
};

static map<string, QueryTypeDescriptor*> *queryTypes = NULL;

static const int MAX_HELPTEXT_LENGTH = 2800;


static void initializeQueryRegistrar() {
	if (queryTypes == NULL)
		queryTypes = new map<string, QueryTypeDescriptor*>();
} // end of initializeQueryRegistrar()


bool registerQueryClass(const char *cmdString, QueryFactoryMethod factoryMethod) {
	initializeQueryRegistrar();
	if (queryTypes->find(cmdString) != queryTypes->end())
		return false;
	QueryTypeDescriptor *qtd = new QueryTypeDescriptor;
	qtd->factoryMethod = factoryMethod;
	qtd->commands.clear();
	qtd->commands.push_back(cmdString);
	qtd->summary = "";
	qtd->helpText = "";
	(*queryTypes)[cmdString] = qtd;
	return true;
} // end of registerQueryClass(char*, ...)


bool registerQueryAlias(const char *cmdString, const char *aliasCmdString) {
	initializeQueryRegistrar();
	if (queryTypes->find(cmdString) == queryTypes->end())
		return false;
	if (queryTypes->find(aliasCmdString) != queryTypes->end())
		return false;
	(*queryTypes)[aliasCmdString] = (*queryTypes)[cmdString];
	(*queryTypes)[aliasCmdString]->commands.push_back(aliasCmdString);
	return true;
} // end of registerQueryAlias(char*, char*)


bool registerQueryHelpText(const char *cmdString, const char *summary, const char *helpText) {
	initializeQueryRegistrar();
	if (queryTypes->find(cmdString) == queryTypes->end())
		return false;
	if ((strlen(summary) > MAX_HELPTEXT_LENGTH) || (strlen(helpText) > MAX_HELPTEXT_LENGTH)) {
		fprintf(stderr, "Help text too long for query type \"%s\". Terminating.\n", cmdString);
		exit(1);
	}
	(*queryTypes)[cmdString]->summary = summary;
	(*queryTypes)[cmdString]->helpText = helpText;
	return true;
} // end of registerHelpText(char*, char*, char*)


char *getQueryHelpText(const char *cmdString) {
	initializeQueryRegistrar();
	if (queryTypes->find(cmdString) == queryTypes->end())
		return NULL;
	else {
		int len = (*queryTypes)[cmdString]->summary.length()
		        + (*queryTypes)[cmdString]->helpText.length() + 256;
		char *result = (char*)malloc(len);
		len = 0;
		len += sprintf(&result[len], "%s - %s\n",
				(*queryTypes)[cmdString]->commands[0].c_str(), (*queryTypes)[cmdString]->summary.c_str());
		if ((*queryTypes)[cmdString]->commands.size() > 1) {
			len += sprintf(&result[len], "  [Aliases:");
			for (int i = 1; i < (*queryTypes)[cmdString]->commands.size(); i++)
				len += sprintf(&result[len], " %s", (*queryTypes)[cmdString]->commands[i].c_str());
			len += sprintf(&result[len], "]\n");
		}
		if ((*queryTypes)[cmdString]->helpText.length() > 0)
			len += sprintf(&result[len], "\n%s", (*queryTypes)[cmdString]->helpText.c_str());
		return result;
	}
} // end of getQueryHelpText(char*)


char *getQueryCommandSummary() {
	initializeQueryRegistrar();
	int allocated = 2 * MAX_HELPTEXT_LENGTH, used = 0;
	char *result = (char*)malloc(allocated);
	used += sprintf(&result[used], "List of available commands:\n\n");
	map<string,QueryTypeDescriptor*>::iterator iter;
	for (iter = queryTypes->begin(); iter != queryTypes->end(); ++iter) {
		// skip aliases; only report the primary command name
		if (iter->first != iter->second->commands[0])
			continue;
		// make sure we have enough space in the output buffer
		if (used + iter->first.length() + iter->second->summary.length() + 128 > allocated) {
			allocated += 2 * MAX_HELPTEXT_LENGTH;
			result = (char*)realloc(result, allocated);
		}
		// print command name and one-line summary
		used += sprintf(&result[used], "  %s - %s\n", iter->first.c_str(), iter->second->summary.c_str());
	}
	used += sprintf(&result[used], "\nFor information about a specific command, type \"@help command-name\".");
	return result;
} // end of getQueryCommandSummary()


QueryFactoryMethod getQueryFactoryMethod(const char *cmdString) {
	initializeQueryRegistrar();
	if (queryTypes->find(cmdString) == queryTypes->end())
		return NULL;
	else
		return (*queryTypes)[cmdString]->factoryMethod;
} // end of getQueryTypeDescriptor(char*)


