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
 * Definition of the DesktopQuery class. DesktopQuery is a generic ranking
 * algorithm used to score "<document!>".."</document!>" extents in the filesystem.
 * DesktopQuery is similar to the QAP passage scorer, with some minor refinements.
 *
 * author: Stefan Buettcher
 * created: 2005-03-16
 * changed: 2007-04-09
 **/


#ifndef __QUERY__DESKTOPQUERY_H
#define __QUERY__DESKTOPQUERY_H


#include "query.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


class DesktopQuery : public RankedQuery {

public:

	/**
	 * For every passage, we return a small snippet that shows the passage in its
	 * context. This is the target size of that snippet.
	 **/
	static const int TARGET_SNIPPET_LENGTH = 50;

	/** Maximum number of tokens in the header field. **/
	static const int HEADER_TOKEN_COUNT = 512;

	/** Maximum number of tokens in the snippet field. **/
	static const int SNIPPET_TOKEN_COUNT = 256;

protected:

	/**
	 * The parameter k1 can be used to set the impact of multiple occurrences of the
	 * same term within a passage. k1 = 0 corresponds to a boolean decision, whereas
	 * k1 = infinity defines a naive TF component. Okapi BM25 uses k1 = 1.2; we have
	 * had good experience with k1 around 1.0 for QAP.
	 **/
	double k1;
	static const double DEFAULT_K1 = 1.2;

	double b;
	static const double DEFAULT_B = 0.75;

	/**
	 * Since getting the snippets for *everything* is pretty expensive, we have the
	 * user client tell us for which part of the result vector he needs the actual
	 * text.
	 **/
	int resultStart, resultEnd;

	/** Tells us whether the query processor has to compute IDF weights or not. **/
	bool noIDF;

	/** List of all "<newpage/>" tags, used to compute page numbers. **/
	ExtentList *pageNumberList;

public:

	DesktopQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~DesktopQuery();

	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	virtual void processModifiers(const char **modifiers);

protected:

	virtual void processCoreQuery();

	virtual void processCoreQueryDocLevel();

private:

	/**
	 * Returns sanitized text corresponding to the given index addresses. Memory
	 * has to be freed by the caller. If "removeNewLines" is true, then any
	 * '\n' characters in the text are replaced by simple whitespace.
	 **/
	char *getText(offset start, offset end, bool removeNewLines);

	/**
	 * Changes the content of string "s" so that it does not contain any occurences
	 * of "!>". Any such substrings will be transformed to " !>". This ensures that
	 * nothing interferes with the markup in the result string we return to the client.
	 * Memory has to be freed by the caller.
	 **/
	char *sanitize(char *s);

}; // end of class DesktopQuery


REGISTER_QUERY_CLASS(DesktopQuery, desktop,
	"Used to realize desktop search queries.",
	"Standard ranked query, returning text from matching documents in addition\n" \
	"to plain relevance scores. Used by the HTTP front-end."
)


#endif


