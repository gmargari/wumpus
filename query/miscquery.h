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
 * The MiscQuery class implements various queries that we need for general
 * maintenance and user interaction, such as:
 *
 * @size, @documents, @files, @whichfile, @addannotation, @getannotation, ...
 *
 * author: Stefan Buettcher
 * created: 2004-09-28
 * changed: 2009-02-01
 **/


#ifndef __QUERY__MISCQUERY_H
#define __QUERY__MISCQUERY_H


#include "query.h"
#include "gclquery.h"
#include "../index/index.h"


class MiscQuery : public Query {

private:

	/** Maximum length of the result line. **/
	static const int MAX_RESULT_LENGTH = 1024;

	char *resultLine;

public:

	MiscQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	~MiscQuery();

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	virtual int getType();

protected:

	virtual void processModifiers(const char **modifiers);

}; // end of class MiscQuery


REGISTER_QUERY_CLASS(MiscQuery, about,
	"Prints copyright information.",
	""
)
REGISTER_QUERY_CLASS(MiscQuery, size,
	"Prints the size of the collection.",
	"Returns the number of tokens in the indexed text collection. The size is\n" \
	"measured by the combined span of all visible files in the collection."
)
REGISTER_QUERY_CLASS(MiscQuery, stem,
	"Prints the stemmed version of the given token sequence.",
	"Stemming is performed using Porter's algorithm (Snowball variant).\n\n" \
	"Example:\n\n" \
	"  @stem information retrieval\n" \
	"  inform retriev"
)
REGISTER_QUERY_CLASS(MiscQuery, files,
	"Prints the number of visible files in the collection.",
	""
)
REGISTER_QUERY_CLASS(MiscQuery, dictionarysize,
	"Prints the size of the internal dictionary (# of terms).",
	"If no exact term count can be obtained (because the Wumpus is maintaining\n" \
	"multiple active index partitions), then a lower and an upper bound are\n" \
	"returned instead."
)
REGISTER_QUERY_CLASS(MiscQuery, fileinfo,
	"Prints type and name of the file corresponding to an index offset.",
	"Examples:\n\n" \
	"  @fileinfo 100\n" \
	"  text/x-trec /home/wumpus/trec.00000.txt\n\n" \
	"  @0-Ok. (1 ms)\n" \
	"  @fileinfo 999999999\n" \
	"  @1-File not found. (0 ms)"
)
REGISTER_QUERY_CLASS(MiscQuery, system,
	"Executes a given command line via system(3).",
	"User must be logged in as engine owner or super-user.\n\n" \
	"Example:\n\n" \
	"  @system cp file1.txt file2.txt\n" \
	"  @0-Ok. (123 ms)\n" \
	"  @addfile file2.txt\n" \
	"  @0-Ok. (234 ms)"
)
REGISTER_QUERY_CLASS(MiscQuery, filestats,
	"Prints a summary of files in the index, split up by file type.",
	""
)
REGISTER_QUERY_CLASS(MiscQuery, summary,
	"Prints a summary of file systems managed by the index.",
	"This information is not useful unless the MasterIndex class is used to manage\n" \
	"index data for multiple file systems."
)


#endif

