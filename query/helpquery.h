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
 * created: 2007-03-13
 * changed: 2009-02-01
 **/


#ifndef __QUERY__HELPQUERY_H
#define __QUERY__HELPQUERY_H


#include "query.h"
#include "gclquery.h"
#include "../index/index.h"


#define HELP_COMMAND "help"


class HelpQuery : public Query {

private:

	char *helpText;

	const char *cmd;

public:

	HelpQuery(Index *index, const char *command, const char **modifiers, const char *body, uid_t userID, int memoryLimit);

	~HelpQuery();

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	static bool isValidCommand(const char *command);

	virtual int getType();

}; // end of class HelpQuery


REGISTER_QUERY_CLASS(HelpQuery, help,
	"Prints help information about various query types.",
	""
)


#endif


