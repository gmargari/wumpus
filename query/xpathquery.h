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
 * Definition of the XPathQuery class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-30
 * changed: 2009-02-01
 **/


#ifndef __QUERY__XPATHQUERY_H
#define __QUERY__XPATHQUERY_H


#include "query.h"
#include "xpath_primitives.h"
#include "../index/index.h"


class XPathQuery : public Query {

private:

	char *errorMessage;

	/** Output of the query. **/
	XMLElementList *resultList;

	static const int MAX_NESTING_LEVEL = 31;

	/** Contains the list of "<level!NNN>" elements for a given level. **/
	ExtentList **openingTagsOnLevel;

	/** Contains the list of "</level!NNN>" elements for a given level. **/
	ExtentList **closingTagsOnLevel;

	/** Number of elements in the result list. **/
	int resultListLength;

	/** Current position in the "resultList" array. **/
	int currentResultPosition;

	/**
	 * Gets set to true if the [getpath] modifier is passed to the query. In that
	 * case, returns the XML path to each result element in addition to its start
	 * and end index address.
	 **/
	bool getPathToResult;

	bool syntaxError;

public:

	XPathQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);
	
	XPathQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~XPathQuery();

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	/**
	 * This method performs the actual execution of the XPath query in a naive
	 * step-by-step fashion.
	 **/
	void executeQuery();

	static bool isValidCommand(char *command);

	/**
	 * Returns an XPath expression that refers to the index extent defined by the
	 * given "start" and "end" positions. Memory has to be freed by the caller.
	 **/
	char *getPathToExtent(offset start, offset end);

private:

	/** Does all the work for the constructors. **/
	void initialize(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents);

	/**
	 * Transforms the query given by "query" into canonical XPath form by replacing
	 * abbreviations by their long forms.
	 * Examples: "//" is replaced by "/descendant-or-self::node()/".
	 *           "/." is replaced by "/self::node()/".
	 *           "/../" is replaced by "/parent::node()/".
	 *
	 * If the query is syntactically incorrect, the method returns NULL.
	 **/
	static char *toCanonicalForm(const char *query);

	/**
	 * Returns the result of the step given by "axis", "nodeText", and
	 * "predicates". If the step is syntactically incorrect, the method returns
	 * NULL.
	 **/
	XMLElementList *processQueryStep(char *axis, char *nodeTest, char *predicates,
			XMLElementList *current, int listPosition);

	/**
	 * Returns the ExtentList instance that represents all opening tags for nodes
	 * on level "level". Do not free this! It belongs to the XPathQuery.
	 **/
	ExtentList *getOpeningTagsOnLevel(int level);

	/**
		* Returns the ExtentList instance that represents all closing tags for nodes
		* on level "level". Do not free this! It belongs to the XPathQuery.
		**/
	ExtentList *getClosingTagsOnLevel(int level);

	XMLElementList * getAncestors(ExtentList *nodeTestOpen, ExtentList *nodeTestClose,
			XMLElementList *current, int pos, int minLevel, int maxLevel);

	XMLElementList * getDescendants(ExtentList *nodeTestOpen, ExtentList *nodeTestClose,
			XMLElementList *current, int pos, int minLevel, int maxLevel);

	void processModifiers(const char **modifiers);

}; // end of class XPathQuery


REGISTER_QUERY_CLASS(XPathQuery, xpath,
	"Executes an XPath query against the index.",
	"This is a very basic version of XPath. Only very few predicates have\n"
	"been implemented. Predicates that have been implemented are usually\n"
	"extremely slow and require lots of memory. Use with caution.\n\n"
	"Example:\n\n"
	"  @gcl[count=1][getxpath] \"<article>\"\n"
	"  16 16 doc(\"/wikipedia/en_wiki.xml\")/doc[1]\n"
	"  @0-Ok. (3 ms)\n"
	"  @xpath[getxpath] doc(\"/wikipedia/en_wiki.xml\")/doc[1]\n"
	"  16 12267 doc(\"/wikipedia/en_wiki.xml\")/doc[1]\n"
	"  @0-Ok. (5 ms)\n"
	"  @xpath[getxpath] doc(\"/wikipedia/en_wiki.xml\")/doc[1]//\n"
	"  16 12267 doc(\"/wikipedia/en_wiki.xml\")/doc[1]\n"
	"  17 19 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/title[1]\n"
	"  20 22 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/id[1]\n"
	"  23 12266 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]\n"
	"  24 26 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/id[1]\n"
	"  27 33 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/timestamp[1]\n"
	"  34 41 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/contributor[1]\n"
	"  35 37 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/contributor[1]/username[1]\n"
	"  38 40 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/contributor[1]/id[1]\n"
	"  42 62 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/comment[1]\n"
	"  63 12265 doc(\"/wikipedia/en_wiki.xml\")/doc[1]/revision[1]/text[1]\n"
	"  @0-Ok. (23 ms)\n"
	"  @get 17 19\n"
	"  <title>Anarchism</title>\n"
	"  @0-Ok. (1 ms)\n\n"
	"Note: If you want to use @xpath queries, you must set ENABLE_XPATH=true in\n"
	"the configuration file *before* you build the index. Changing the variable\n"
	"will make Wumpus put additional information in the index that is required\n"
	"for processing XPath queries."
)


#endif


