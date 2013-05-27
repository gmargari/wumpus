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
 * The UpdateQuery class implements all the update operations we need:
 * @addfile, @removefile, ...
 *
 * author: Stefan Buettcher
 * created: 2004-10-11
 * changed: 2009-02-01
 **/


#ifndef __QUERY__UPDATEQUERY_H
#define __QUERY__UPDATEQUERY_H


#include "query.h"
#include "../index/index.h"


class UpdateQuery : public Query {

private:

	char returnString[256];

	int statusCode;

public:

	UpdateQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~UpdateQuery();

	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	static bool isValidCommand(const char *command);

	virtual int getType();

}; // end of class UpdateQuery


REGISTER_QUERY_CLASS(UpdateQuery, addfile,
	"Adds the contents of the given file to the index.",
	"File name may be absolute or relative. May contain wildcard characters.\n\n" \
	"Example:\n\n" \
	"  @addfile[text/xml] test.txt\n\n" \
	"Query modifiers supported:\n" \
	"  [FILE_TYPE] -- used to force Wumpus to use a specific input tokenizer;\n" \
	"    if none is given, Wumpus will try to auto-detect the file type"
)
REGISTER_QUERY_CLASS(UpdateQuery, removefile,
	"Removes a previously indexed file from the index.",
	"File name may be absolute or relative. May not contain wildcard characters.\n\n" \
	"Note that the index data for the given file are not actually removed, but\n" \
	"are just no longer visible to the query processor. They will be physically\n" \
	"removed from the index when the garbage collector is run the next time.\n\n" \
	"Example:\n\n" \
	"  @removefile test.txt"
)
REGISTER_QUERY_CLASS(UpdateQuery, rename,
	"Informs Wumpus that the name or path of the given file has changed.",
	"Example:\n\n" \
	"  @rename /var/log/messages /var/log/messages.1"
)
REGISTER_QUERY_CLASS(UpdateQuery, updateattr,
	"Makes Wumpus update its internal information about a given file.",
	"The @updateattr query is normally used after chown or chmod operations to\n" \
	"keep the index in sync with the new state of the file system.\n" \
	"The given filename may be absolute or relative. Wildcards are not supported.\n\n" \
	"Example:\n\n" \
	"  @updateattr /var/log/messages"
)
REGISTER_QUERY_CLASS(UpdateQuery, sync,
	"Syncs the in-memory index with the on-disk index.",
	"The @sync command forces Wumpus to to brings the on-disk index structures in\n" \
	"sync with the index data pending in memory, most likely by performing a merge\n" \
	"operation."
)


#endif


