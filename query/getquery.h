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
 * Definition of the GetQuery class. GetQuery is used to answers requests
 * of the type @get or @get[filtered].
 *
 * author: Stefan Buettcher
 * created: 2004-10-03
 * changed: 2009-02-01
 **/


#ifndef __QUERY__GETQUERY_H
#define __QUERY__GETQUERY_H


#include "query.h"
#include "../filters/inputstream.h"
#include "../filemanager/filemanager.h"
#include "../index/index.h"
#include "../index/index_types.h"


class GetQuery : public Query {

private:

	/** Desired index range exists? File permissions are ok? **/
	bool permissionDenied, fileError;

	/** Tells us if the user wants filtered or unfiltered output. **/
	bool filtered;

	offset startOffset, endOffset;

	/** File numbers of the files requested by the offsets given. **/
	int startFile, endFile;

	/** Type of the file we are looking into. **/
	int documentType;

	/** Name of the file where the data comes from. **/
	char *fileName;

	/**
	 * A list of sequenceNumber/filePosition pairs that helps us speed up
	 * the processing of the query.
	 **/
	TokenPositionPair *tppSpeedup;

public:

	GetQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	GetQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	GetQuery(Index *index, offset start, offset end, bool filtered);

	/** Default destructor. **/
	~GetQuery();

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	static bool isValidCommand(const char *command);

	virtual int getType();

protected:

	virtual void processModifiers(const char **modifiers);

private:

	void initialize(Index *index, const char *command, const char **modifiers, const char *body,
	                VisibleExtents *visibleExtents);

}; // end of class GetQuery


REGISTER_QUERY_CLASS(GetQuery, get,
	"Prints the text stored at a given index range.",
	"Examples:\n\n" \
	"  @get 1097704 1097710\n" \
	"  An example from the past: American steelmakers.\n" \
	"  @0-Ok. (1 ms)\n" \
	"  @get[filtered] 1097704 1097710\n" \
	"  an example from the past american steelmakers\n" \
	"  @0-Ok. (2 ms)\n\n" \
	"Query modifiers supported:\n\n" \
	"  boolean filtered (default: false)\n" \
	"    Affects the output of @get as shown above."
)


#endif


