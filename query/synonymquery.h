/**
 * Copyright (C) 2010 Stefan Buettcher. All rights reserved.
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
 * Definition of the SynonymQuery class. The class can be used to find
 * synonyms for a given term within a given context.
 *
 * author: Stefan Buettcher
 * created: 2010-03-06
 * changed: 2010-03-06
 **/

#ifndef __QUERY__SYNONYMQUERY_H
#define __QUERY__SYNONYMQUERY_H

#include <queue>
#include <string>
#include <vector>
#include "query.h"

class SynonymQuery : public Query {
public:
	SynonymQuery(Index *index, const char *command, const char **modifiers,
		const char *body, uid_t userID, int memoryLimit);

	~SynonymQuery();

	/** Returns true iff the given query string is syntactically correct. **/
	virtual bool parse();

	virtual bool getNextLine(char *line);

	virtual bool getStatus(int *code, char *description);

	virtual int getType();

protected:
	virtual void processModifiers(const char **modifiers);

	/**
	 * Return an executed BM25Query object for the given query terms (Boolean AND);
	 * NULL upon error.
	 **/
	RankedQuery *getRankedQuery(const std::vector<std::string> &retrievalTerms,
	                            const std::vector<std::string> &scoringTerms);

	/** Returns the fraction of the result lists of r1 and r2 that overlap. **/
	double getOverlap(RankedQuery *r1, RankedQuery *r2);

	/** Return a language model for the documents in r's result list. **/
	LanguageModel *getLanguageModel(RankedQuery *r);

private:
	/** A list of context terms to be used when searching for synonyms. **/
	std::vector<std::string> contextTerms;

	/** The list of synonyms for the given term. **/
	std::queue<std::string> resultLines;

	/** Number of documents to use for feedback and number of candidate terms to extract. **/
	int feedbackDocs, feedbackTerms;
}; // end of class SynonymQuery

REGISTER_QUERY_CLASS(SynonymQuery, get_synonyms,
	"Gets a list of synonyms for the given term in the given context.",
	"Example:\n\n" \
	"  @get_synonyms[context=michigan] \"car\""
)
REGISTER_QUERY_ALIAS(get_synonyms, get_syns)

#endif // __QUERY__SYNONYMQUERY_H

