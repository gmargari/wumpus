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
 * Definition of the TierQuery class. TierQuery implements cover density ranking
 * with term coordination level, as presented by Cormack, Clarke, and Tudhope
 * ("Relevance Ranking for One to Three Term Queries").
 *
 * author: Stefan Buettcher
 * created: 2005-10-12
 * changed: 2009-02-01
 **/


#ifndef __QUERY__TIERQUERY_H
#define __QUERY__TIERQUERY_H


#include "cdrquery.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


typedef struct {
	ExtentList *list;
	double score;
} ScoredQuery;


class TierQuery : public CDRQuery {

protected:

	/** Length of the passage used to compute QAP-like query scores. **/
	static const int PASSAGE_LENGTH = 64;

public:

	TierQuery();

	/** Creates a new TierQuery instance. **/
	TierQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	TierQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	virtual ~TierQuery();

	virtual bool parse();

protected:

	virtual void processCoreQuery();

private:

	void getSubQuery(int n, ScoredQuery *q);

}; // end of class TierQuery


#endif


