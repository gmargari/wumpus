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
 * Definition of the CountQuery class. CountQuery takes care of:
 * 
 *  @count, @estimate, @documents, @docs, @documentsContaining
 *
 * author: Stefan Buettcher
 * created: 2004-09-26
 * changed: 2009-02-01
 **/


#ifndef __QUERY__COUNTQUERY_H
#define __QUERY__COUNTQUERY_H


#include "query.h"
#include "gclquery.h"
#include "../index/index.h"


class CountQuery : public Query {

private:

	/**
	 * The underlying GCL queries for which we want to get the count value. We
	 * can process multiple GCL expressions at once:
	 *
	 *   @count "man", "woman", "sex", "children"
	 **/
	GCLQuery **subQueries;

	/** Number of sub-queries. **/
	int subQueryCount;

	/** Indicates an @size query. @size calls getTotalSize() instead of getLength(). **/
	bool returnSize;

	/** Tells us whether [avg] was given with @size. **/
	bool returnAverage;

	/** True iff this query is an @histogram query. **/
	bool isHistogram;

public:

	CountQuery(Index *index, const char *command, const char **modifiers,
		const char *body, int userID, int memoryLimit);

	virtual ~CountQuery();

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	static bool isValidCommand(const char *command);

protected:

	virtual void processModifiers(const char **modifiers);

}; // end of class CountQuery


REGISTER_QUERY_CLASS(CountQuery, count,
	"Returns the number of matches for a given GCL expression.",
	"Examples:\n\n" \
	"  @count (((\"mother\"^\"father\")+\"parents\")..\"children\")<[10]\n" \
	"  30\n" \
	"  @0-Ok. (2 ms)\n" \
	"  @count[size] (((\"mother\"^\"father\")+\"parents\")..\"children\")<[10]\n" \
	"  156\n" \
	"  @0-Ok. (2 ms)\n" \
	"  @count[avgsize] (((\"mother\"^\"father\")+\"parents\")..\"children\")<[10]\n" \
	"  5.2\n" \
	"  @0-Ok. (2 ms)\n" \
	"  @count \"this\", \"and\", \"that\"\n" \
	"  10879, 81435, 41362\n" \
	"  @0-Ok. (6 ms)\n\n" \
	"Query modifiers supported:\n" \
	"  boolean size (default: false)\n" \
	"    if set, the search engine returns the total size of all matches\n" \
	"  boolean avgsize (default: false)\n" \
	"    if set, the search engine returns the average size of all matches"
)
REGISTER_QUERY_ALIAS(count, estimate)

REGISTER_QUERY_CLASS(CountQuery, histogram,
	"Prints statistical info about passages matching a GCL expression.",
	"Example:\n\n" \
	"  @histogram (\"mother\"^\"father\")\n" \
	"  123 24 694753 850.925\n" \
	"  0\n" \
	"  0\n" \
	"  3\n" \
	"  3\n" \
	"  3\n" \
	"  3\n" \
	"  4\n" \
	"  4\n" \
	"  4\n" \
	"  6\n" \
	"  7\n" \
	"  ...\n\n" \
	"In the above example, there are 123 matches in total. The length of the\n" \
	"longest matching passage report in the statistics is 24. The total length\n" \
	"of all matches is 694753 (tokens). The sum of the logs of the lengths is\n" \
	"850.925 (tokens). There are 3 matches within 3 words, 4 matches within 7\n" \
	"words, 6 matches within 10 words, and so on."
)

REGISTER_QUERY_CLASS(CountQuery, documents,
	"Returns the number of doc's in a given TREC-formatted collection.",
	"@documents is a shortcut for @count \"<doc>\"..\"</doc>\"."
)

REGISTER_QUERY_CLASS(CountQuery, documentsContaining,
	"Prints the number of doc's matching a given GCL expr'n.",
	"This is a shortcut for @count (\"<doc>\"..\"</doc>\")>(EXPRESSION).\n\n" \
	"Examples:\n\n" \
	"  @documentsContaining \"very\"^\"interesting\"\n" \
	"  43\n" \
	"  @0-Ok. (52 ms)\n"
	"  @documentsContaining \"very\", \"interesting\"\n" \
	"  1258, 104\n" \
	"  @0-Ok. (63 ms)"
)
REGISTER_QUERY_ALIAS(documentsContaining, docs)

#endif


