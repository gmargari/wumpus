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
 * Definition of the GCLQuery class. GCLQuery is used to parse GCL-type
 * queries submitted by the user.
 *
 * author: Stefan Buettcher
 * created: 2004-09-24
 * changed: 2009-02-01
 **/


#ifndef __QUERY__GCLQUERY_H
#define __QUERY__GCLQUERY_H


#include "query.h"
#include "../index/index.h"


class XPathQuery;


class GCLQuery : public Query {

public:

	/** Maximum number of characters returned by @gcl[get]. **/
	static const int MAX_GET_LENGTH = 256;

	/** Output of the query. **/
	ExtentList *resultList;

private:

	/**
	 * Needed to produce the next output line (we remember the last index position
	 * we have seen).
	 **/
	offset currentResultPosition;

	/**
	 * If this is true (set by the modifier "[get]"), the actual text is returned
	 * for every index extent matching the GCL expression.
	 **/
	bool getText, getFiltered;

	/**
	 * If this is set to true (via modifier [getxpath]), then we also report the
	 * XPath expression that corresponds to each result extent.
	 **/
	bool getXPath;

	/** Used to get the XPath expression for a result extent. **/
	XPathQuery *xpathQuery;

	/**
	 * Tells us whether the result of the query (the ExtentList produced) has to
	 * be strictly secure. In many scenarios, it is acceptable if it is only almost
	 * secure, for example if the count the number of occurrences of a term inside
	 * a document.
	 **/
	bool hasToBeSecure;

public:

	/** Create a pseudo-query from an already existing result list. **/
	GCLQuery(Index *index, ExtentList *result);

	/**
	 * Creates a new GCLQuery instance. All the results will be filtered against the
	 * VisibleExtents instance given by "visibleExtents".
	 **/
	GCLQuery(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visExt, int memoryLimit);

	/**
	 * Creates a new GCLQuery instance for the user with ID "userID". If this constructor
	 * is used, the list of visible extents will be automatically computed from the UID.
	 **/
	GCLQuery(Index *index, const char *command, const char **modifiers,
			const char *body, uid_t userID, int memoryLimit);

	/** Default destructor. **/
	~GCLQuery();

	/** Returns true iff command equals "gcl" (case-insensitive). **/
	static bool isValidCommand(const char *command);

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	/**
	 * Returns an ExtentList instance that contains the extents that match the
	 * query or NULL if there is no result (parse error).
	 **/
	ExtentList *getResult();

	/**
	 * Tells the GCLQuery instance that the result does not have to be strictly
	 * secure. An almost secure result will do.
	 **/
	void almostSecureWillDo();

	/** Returns the query string. To be freed by the caller. **/
	char *getQueryString();

	/**
	 * Tells us whether the GCL query string given by "gclString" is a simple
	 * term or a more complicated structure (OR, AND, sequence, ...).
	 **/
	static bool isSimpleTerm(char *gclString);

	/**
	 * Transforms the given query string to normal form. This includes removal of
	 * redundant whitespace characters and case folding.
	 **/
	static char *normalizeQueryString(const char *queryString);

	/**
	 * Used to exchange the current result list by a new one. The query instance
	 * will take control of the given list, so don't touch it after calling this
	 * function.
	 **/
	void setResultList(ExtentList *list);

protected:

	void processModifiers(const char **modifiers);

private:

	/**
	 * This method is used by the constructors to do some, surprise! surprise!,
	 * initialization.
	 **/
	void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents);

	ExtentList *parseAndReturnList(Index *index, char *query, int memoryLimit);

	ExtentList *createTermSequence(Index *index, char *query, int memoryLimit);

	/**
	 * Returns a list of files satisfying the condition given by "restriction".
	 * Restrictions are generally of the form:
	 *  [filesize = 12345]
	 *  [filetype = text/html]
	 *  [filetype in text/html, text/plain, application/pdf]
	 **/
	ExtentList *processFileRestriction(char *restriction);

	/**
	 * Removes heading and trailing whitespaces. Removes pairs of brackets.
	 * Memory has to be freed by the caller.
	 **/
	static char *normalizeString(char *s);

}; // end of class GCLQuery


REGISTER_QUERY_CLASS(GCLQuery, gcl,
	"Runs a standard GCL query against the data in the index.",
	"For a thorough description of the GCL query language, have a look at\n" \
	"Clarke et al., \"An Algebra for Structured Text Search and a Framework for\n" \
	"its Implementation\". The Computer Journal, 38(1):43-56, 1995.\n" \
	"@gcl is the standard query type. That is, if unspecified, @gcl is assumed.\n\n" \
	"Examples:\n\n" \
	"  @gcl[get][count=3] (\"because\"^\"of\")<[5]\n" \
	"  1158 1161 \"because the window of\"\n" \
	"  1569 1573 \"of R.H. Macy because\"\n" \
	"  1573 1574 \"because of\"\n" \
	"  @0-Ok. (124 ms)\n" \
	"  \"later that day\"\n" \
	"  2880204 2880206\n" \
	"  3560135 3560137\n" \
	"  3897696 3897698\n" \
	"  @0-Ok. (3 ms)\n\n" \
	"Operators supported:\n\n" \
	"  \"^\" (Boolean AND), \"+\" (Boolean OR), \">\" (CONTAINS),\n" \
	"  \"/>\" (DOES-NOT-CONTAIN), \"<\" (CONTAINED-IN), \"/<\" (NOT-CONTAINED-IN),\n" \
	"  \"..\" (FOLLOWED-BY), [N] (window of N char's), N (absolute index address)\n\n"
	"In addition to the canonical GCL operators, Wumpus also understands extended\n" \
	"restrictions based on file-related meta-data, for example:\n\n" \
	"  {filetype=text/xml} matches all files of type text/xml\n" \
	"  {filesize > 100000} matches all files bigger than 100,000 bytes\n" \
	"  {filepath=/home/wumpus/*} matches all files below the given directory\n" \
	"  \"<file!>\" returns the start offset of all visible files\n" \
	"  \"</file!>\" returns the end offset of all visible files\n\n" \
	"Query modifiers supported:\n\n" \
	"  boolean get (default: false)\n" \
	"    returns the text at each matching index position\n" \
	"  boolean filtered (default: false)\n" \
	"    to be used in conjunction with [get]: does not return the original text,\n" \
	"    but the text after being run through Wumpus' input tokenizer\n" \
	"  boolean getxpath (default: false)\n" \
	"    prints an XPath expression for each given index position returned; only\n" \
	"    works if the ENABLE_XPATH configuration variable was set when building\n" \
	"    the index\n" \
	"  For further modifiers, see \"@help query\".\n"
)


#endif


