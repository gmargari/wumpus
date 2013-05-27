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
 * Definition of the BM25Query class. BM25Query implements the Okapi BM25
 * document scoring function.
 *
 * author: Stefan Buettcher
 * created: 2004-09-26
 * changed: 2009-02-01
 **/


#ifndef __QUERY__BM25QUERY_H
#define __QUERY__BM25QUERY_H


#include "query.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


class BM25Query : public RankedQuery {

public:

	/** These are the standard Robertson/Walker parameters for BM25. **/
	static const double DEFAULT_K1 = 1.2;
	static const double DEFAULT_B = 0.75;

	/** Our BM25 implementation will not score containers that are smaller than this. **/
	static const int MIN_OKAPI_CONTAINER_SIZE = 4;

protected:

	/** BM25 parameters. **/
	double k1, b;

	/** If this is true, IDF weights are all set to 1. **/
	bool noIDF;

	/** If this is true, we use IDF exclusively (no TF). **/
	bool noTF;

	/**
	 * Used to switch term proximity scoring on or off. Turned off by default.
	 * Set through query modifier.
	 **/
	bool useTermProximity;

	/**
	 * Weight given to term rank evidence (i.e., position of first occurrence of
	 * query term within document). Set to zero by default. Set to different value
	 * through query modifier.
	 **/
	double chronologicalTermRank;

public:

	/** Stupid default constructor. **/
	BM25Query() { }

	BM25Query(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	BM25Query(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~BM25Query();

	static double getScore(double tf, double k1, double b, double dl, double avgdl) {
		double K = k1 * (1 - b + b * dl / avgdl);
		return tf * (k1 + 1.0) / (tf + K);
	}

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

}; // end of class BM25Query


REGISTER_QUERY_CLASS(BM25Query, bm25,
	"Performs Okapi BM25 relevance ranking.",
	"BM25 follows the standard syntax of most other ranked queries.\n" \
	"(see \"@help rank\" for details)\n\n" \
	"Query modifiers supported:\n" \
	"  float k1 (default: 1.2)\n" \
	"    BM25 TF fan-out parameter\n" \
	"  float b (default: 0.75)\n" \
	"    BM25 document length normalization parameter\n" \
	"  boolean noidf (default: false)\n" \
	"    flag used to prevent the query processor from multiplying the term weights\n" \
	"    given in the query string with term-specific IDF values\n" \
	"  boolean notf (default: false)\n" \
	"    flag used to prevent the query processor from using TF information\n" \
	"  boolean tp (default: false)\n" \
	"    flag used to run BM25TP (with term proximity) instead of ordinary BM25;\n" \
	"    see Buettcher et al., \"Term proximity scoring...\", SIGIR 2006, for details\n" \
	"  float ctr (default: 0)\n" \
	"    set to non-zero value to switch on chronological term rank (cf.\n" \
	"    Troy, Zhang, \"Enhancing relevance scoring with chronological term rank\",\n" \
	"    SIGIR 2007); the method implemented here is their [B,h] variant with D=30;\n" \
	"    the value of \"ctr\" is used as weight for the term rank component (\"C\")\n" \
	"  For further modifiers, see \"@help rank\".\n"
)
REGISTER_QUERY_ALIAS(bm25, okapi)


#endif


