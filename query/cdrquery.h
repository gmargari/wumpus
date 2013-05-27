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
 * Definition of the CDRQuery class. CDRQuery implements cover density ranking,
 * as presented by Cormack, Clarke, and Tudhope ("Relevance Ranking for One to
 * Three Term Queries").
 *
 * author: Stefan Buettcher
 * created: 2005-10-12
 * changed: 2009-02-01
 **/


#ifndef __QUERY__CDRQUERY_H
#define __QUERY__CDRQUERY_H


#include "query.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


class CDRQuery : public RankedQuery {

protected:

	/**
	 * This is the K parameter that is used by Cormack et al. to assign scores
	 * to covers.
	 **/
	double K;

	/** Default value for the K parameter. **/
	static const double DEFAULT_K = 16;

	/** Number of query subsets to consider during the ranking process. **/
	int maxLevel;
	
	/** Maximum number of query terms supported. **/
	static const int CDR_MAX_SCORER_COUNT = 8;

	static const int DEFAULT_MAX_LEVEL = (1 << CDR_MAX_SCORER_COUNT);

public:

	CDRQuery();

	/** Creates a new CDRQuery instance. **/
	CDRQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	CDRQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	virtual ~CDRQuery();

	virtual bool parse();

protected:

	virtual void processCoreQuery();

	virtual void processModifiers(const char **modifiers);

	virtual void initialize(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

}; // end of class CDRQuery


REGISTER_QUERY_CLASS(CDRQuery, cdr,
	"Cover density ranking.",
	"The @cdr command starts a cover density ranking process, as defined by\n" \
	"Clarke et al., \"Relevance Ranking for One to Three Term Queries\".\n" \
	"Information Processing and Management 36(2), 291-311, 2000.\n" \
	"The query syntax is the same as for all other ranked queries (@help rank),\n" \
	"but @cdr does not support more than 5 query terms.\n\n" \
	"Given a set of query terms Q1, ..., Qn, @cdr builds a Boolean AND for all\n" \
	"subsets (e.g., \"Q1^Q2^Q5\") and ranks these subsets by the sum of their\n" \
	"terms' IDF values. It then ranks all documents based on the rank of the subset\n" \
	"they contain (\"Q1^Q2^...^Qn\" ranked highest). Documents at the same\n" \
	"level are ranked according to the rules described by Clarke et al..\n\n" \
	"Modifiers supported:\n" \
	"  double K (default: 16)\n" \
	"    model parameter defining the decay of the proximity component\n" \
	"  int maxlevel (default: 32)\n" \
	"    used to limit the scoring process to the top \"maxlevel\" subsets when\n" \
	"    subsets are ranked according to the sum of their terms' IDF values\n" \
	"  boolean strict (default: false)\n" \
	"    shortcut for [maxlevel=1]\n" \
	"  For further modifiers, see \"@help rank\".\n"
)

#endif


