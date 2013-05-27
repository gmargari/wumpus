/**
 * Copyright (C) 2008 Stefan Buettcher. All rights reserved.
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
 * author: Stefan Buettcher
 * created: 2008-08-14
 * changed: 2009-02-01
 **/


#ifndef __TERABYTE__CHAPTER6_H
#define __TERABYTE__CHAPTER6_H


#include "terabyte.h"
#include "../index/index.h"
#include "../query/rankedquery.h"
#include "../query/gclquery.h"


class Chapter6 : public RankedQuery {

public:

	Chapter6(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	Chapter6(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~Chapter6();

protected:

	virtual void processModifiers(const char **modifiers);

	virtual void processCoreQuery();

	virtual GCLQuery *createElementQuery(const char *query, double *weight, int memoryLimit);

private:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	void computeTermWeights(ExtentList **elementLists, double containerCount);

	void executeQuery_Ntoulas();

	void executeQuery_Conjunctive();

	void executeQuery_DocumentAtATime();

	void executeQuery_TermAtATime();

	/** The k1 parameter in the BM25 ranking formula. **/
	float k1;

	/** The b parameter in the BM25 ranking formula. **/
	float b;

	/** Whether to compute Ntoulas's correctness indicator. **/
	bool ntoulas;

	/** Whether we are in AND mode or in OR mode. **/
	bool conjunctive;

	/** Whether to use term-at-a-time or document-at-a-time. **/
	bool termAtATime;

	/** Whether to use the MaxScore heuristic. **/
	bool useMaxScore;

	/** Maximum number of accumulators for term-at-a-time. **/
	int accumulatorLimit;

}; // end of class TerabyteQuery

REGISTER_QUERY_CLASS(Chapter6, chapter6,
	"Performs BM15 relevance ranking on a frequency index.",
	"@bm15tera follows the standard syntax of most other ranked queries.\n" \
	"(see \"@help rank\" for details)\n\n" \
	"Query modifiers supported:\n" \
	"  float k1 (default: 1.2)\n" \
	"    BM25 TF fan-out parameter\n" \
	"  float b (default: 0.75)\n" \
	"    BM25 document length normalization parameter\n" \
	"  bool term_at_a_time (default: false)\n" \
	"    whether to process the query document-at-a-time or term-at-a-time\n" \
	"  bool use_max_score (default: false)\n" \
	"    whether to employ the MaxScore heuristic\n" \
	"  For further modifiers, see \"@help rank\".\n"
)


#endif

