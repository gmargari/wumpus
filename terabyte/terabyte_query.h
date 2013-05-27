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
 * Definition of the TerabyteQuery class. TerabyteQuery is an implementation
 * of Okapi BM25, fine-tuned for extreme performance, to be used in the TREC
 * Terabyte track.
 * 
 * author: Stefan Buettcher
 * created: 2005-05-25
 * changed: 2009-02-01
 **/


#ifndef __TERABYTE__TERABYTE_QUERY_H
#define __TERABYTE__TERABYTE_QUERY_H


#include "terabyte.h"
#include "../index/compactindex.h"
#include "../index/index.h"
#include "../query/bm25query.h"
#include "../query/gclquery.h"


/**
 * These constants define how much memory we allocate for cached impact values
 * (part of collection statistics).
 **/
static const int MAX_CACHED_TF = DOC_LEVEL_MAX_TF;
static const int MAX_CACHED_SHIFTED_DL = 2048;


/**
 * This structure is used to speed up query processing by using cached
 * collection statistics. Data are stored in and retrieved from the Index's
 * IndexCache instance.
 **/
typedef struct {

	/**
	 * Okapi parameters for which the data found here were computed. We need to
	 * store this information so that we can recompute the impact values when the
	 * front-end changes the Okapi parameters.
	 **/
	double k1, b;

	/** Number of documents in the collection. **/
	unsigned int documentCount;

	/** Average document length in tokens. **/
	float avgDocumentLength;

	/**
	 * This is for precomputed TF-impact values. We take the length of the
	 * document and shift it (">>") so that we can immediately look up the
	 * impact up in the "tfImpactValues" array.
	 **/
	int documentLengthShift;

	/**
	 * Tells us the score impact that X occurrences of a term within a document
	 * of shift-adjusted length Y would have: tfImpactValue[Y][X].
	 **/
	float tfImpactValue[MAX_CACHED_SHIFTED_DL + 1][MAX_CACHED_TF + 1];

} TerabyteCachedDocumentStatistics;


typedef struct {

	/** Index that is to be used to fetch the posting list. **/
	Index *index;

	/** Reference to the pruned in-memory index for frequent terms. **/
	CompactIndex *inMemoryIndex;

	/** The scorer whose posting list is to be fetched. **/
	GCLQuery *query;

	/**
	 * Tells the fetcher whether it has to fetch an ordinary posting list or a
	 * document-level list.
	 **/
	bool isDocumentLevel;

	/** Tells us whether this posting list has been fetched from the in-mem index. **/
	bool fromInMemoryIndex;

} TerabyteQueryTerm;


class TerabyteQuery : public BM25Query {

public:

	/** Our BM25 implementation will not score containers that are smaller than this. **/
	static const int MIN_OKAPI_CONTAINER_SIZE = 32;

	/** Ad hoc solution for speedup through in-memory index. **/
	static CompactIndex *inMemoryIndex;

	/**
	 * Tells us whether the query has to load the in-memory index. This is initialized
	 * to "true". After the first attempt to load the in-memory index (whether successful
	 * or not), it is set to "false" in order to avoid subsequent unsuccessful attempts
	 * to load it.
	 **/
	static bool mustLoadInMemoryIndex;

	static const int FEEDBACK_NONE = 0;
	static const int FEEDBACK_OKAPI = 1;
	static const int FEEDBACK_WATERLOO = 2;

	static const int FEEDBACK_DOCUMENT_COUNT = 10;
	static const int FEEDBACK_EXPANSION_TERM_COUNT = 20;

	static const int RERANK_SURROGATE_NONE = 0;
	static const int RERANK_SURROGATE_COSINE = 1;
	static const int RERANK_SURROGATE_KLD = 2;

private:

	/**
	 * Tells us whether this query is an ordinary query using positional information
	 * or a document-level query.
	 **/
	bool isDocumentLevel;

	/** Do we want pseudo-relevance feedback? If yes, which method? **/
	int pseudoRelevanceFeedback;

	/**
	 * Tells us whether the postings contain any positional information or whether
	 * they are just document IDs, enriched with term frequency values.
	 **/
	bool positionless;

	/**
	 * Defined the surrogate-based reranking technique to be used by the query
	 * processor. Options are: RERANK_SURROGATE_NONE, RERANK_SURROGATE_COSINE,
	 * RERANK_SURROGATE_KLD.
	 **/
	int surrogateMode;

public:

	TerabyteQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	TerabyteQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~TerabyteQuery();

	virtual bool parse();

	/**
	 * Sets the internal query terms of the TerabyteQuery. You need to do this
	 * before calling the parse() method, because otherwise the query has already
	 * been processed. The TerabyteQuery instance will take control of all lists
	 * found in the given array and delete them upon completion. It does, however,
	 * not deallocate the array itself.
	 **/
	void setScorers(ExtentList **scorers, int scorerCount);

protected:

	virtual bool parseScorers(const char *scorers, int memoryLimit);

	virtual void processModifiers(const char **modifiers);

	virtual void processCoreQuery();

private:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/**
	 * Used if document-level postings are available. This gives us a dramatic speedup
	 * because of all the tiny tweaks that we can use when we have document-level postings
	 * (computing upper bounds for the score of the current document without knowing its
	 * size, etc.), especially when used in conjunction with a restricted in-memory index.
	 **/
	void executeQueryDocLevel();

	void executeQueryDocLevel_TermAtATime();

	/**
	 * No document-level postings are available -- :-(. Instead of just implementing a
	 * slower version of executeQueryDoclevel, a slightly different approach is taken
	 * here, combining Okapi BM25 with proximity ranking methods.
	 **/
	void executeQueryWordLevel();

	/**
	 * Computes average document lengths and cached TF impact values for various
	 * relative document lengths (compared to the average document length). Everything
	 * is put into the cache in order to be available for the next query.
	 **/
	void computeCollectionStats(ExtentList *containerList, IndexCache *cache);

}; // end of class TerabyteQuery

REGISTER_QUERY_CLASS(TerabyteQuery, bm25tera,
	"Performs Okapi BM25 relevance ranking on a frequency index or a standard\n" \
	"    schema-independent index.",
	"@bm25tera follows the standard syntax of most other ranked queries.\n" \
	"(see \"@help rank\" for details)\n\n" \
	"Query modifiers supported:\n" \
	"  float k1 (default: 1.2)\n" \
	"    BM25 TF fan-out parameter\n" \
	"  float b (default: 0.75)\n" \
	"    BM25 document length normalization parameter\n" \
	"  boolean tp (default: false)\n" \
	"    flag used to run BM25TP (with term proximity) instead of ordinary BM25;\n" \
	"    see Buettcher et al., \"Term proximity scoring...\", SIGIR 2006, for details\n" \
	"  For further modifiers, see \"@help rank\".\n"
)
																								


#endif


