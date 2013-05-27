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
 * Definition of the RankedQuery class. RankedQuery is responsible for all types
 * of relevance queries supported by Wumpus. Relevance queries come in the form:
 *
 *   @rank[TYPE] [CONTAINER_QUERY by] SCORER_1, .., SCORER_N [with weights from STATS_QUERY]
 *
 * CONTAINER_QUERY is something like "<doc>".."</doc>". The scorers are the query
 * terms (but can be arbitrary GCL expressions). If STATS_QUERY is specified,
 * STATS_QUERY is used to compute query term weights (instead of taking weights
 * from term occurrences in CONTAINER_QUERY.
 *
 * author: Stefan Buettcher
 * created: 2004-09-27
 * changed: 2009-02-22
 **/


#ifndef __QUERY__RANKEDQUERY_H
#define __QUERY__RANKEDQUERY_H


#include "query.h"
#include "gclquery.h"
#include "../feedback/feedback.h"
#include "../index/index_types.h"
#include <math.h>


/**
 * We have to keep track of our results and sort them. This is what we use
 * this structure for.
 **/
typedef struct {
	offset from;
	offset to;
	offset containerFrom;
	offset containerTo;
	float score;
	int additional;
} ScoredExtent;


class RankedQuery : public Query {

public:

	/** Used for normalization of the self-information scores. **/
	static const float LOG_2 = 0.693147;

	/**
	 * We do not accept query term weights ("#x.xxx QUERY_TERM") greater than
	 * this value.
	 **/
	static const double MAX_QTW = 10000;

	/** Maximum number of query terms (GCL expressions) per query. **/
	static const int MAX_SCORER_COUNT = 512;

protected:

	/** The actual Query object used: BM25Query, QAPQuery, ... **/
	RankedQuery *actualQuery;

	/** Query ID string, as given by "[id=...]". **/
	char *queryID;

	/** TREC-style run ID. **/
	char *runID;

	/** Query defining the list of candidate extents. **/
	GCLQuery *containerQuery;

	/** Query used to compute collection statistics and term weights. **/
	GCLQuery *statisticsQuery;

	/** Number of element queries (scorers). **/
	int elementCount;
 
	/** In case we use feedback, this is the original number of query terms. **/
	int originalElementCount;

	/** External weights of all query terms, as specified by the user. **/
	double externalWeights[MAX_SCORER_COUNT];

	/** Internal weights (e.g., BM25 weights) of all query terms. **/
	double internalWeights[MAX_SCORER_COUNT];

	/** List of the individual element scorers (GCL expressions). **/
	GCLQuery *elementQueries[MAX_SCORER_COUNT];

	/** A sorted list of scored extents. **/
	ScoredExtent *results;

	/** Our current position in the output list (offset in "results" array). **/
	int position;

	/** Are we doing pseudo-relevance feedback? **/
	int feedbackMode;

	/** Number of terms and documents used in pseudo-relevance feedback. **/
	int feedbackTerms, feedbackDocs;

	/** The external weight of the feedback terms. **/
	double feedbackTermWeight;

	/** Whether to adjust the weights of the original query terms. **/
	bool feedbackReweightOrig;

	/** Whether to treat stem-equivalent terms as equal for the purpose of feedback. **/
	bool feedbackStemming;

	/** A qrels file for explicit relevance feedback. **/
	char *feedbackQrels;

	/** Do we use result reranking techniques? **/
	int performReranking;

	/**
	 * In case performReranking == RERANKING_KLD, this variable tells us how we have
	 * to construct the relevance model. Possible values are given by the METHOD_*
	 * constants in the RelevanceModel class.
	 **/
	int relevanceModelMethod;

	/**
	 * Possible reranking techniques:
	 * - RERANKING_KLD: KLD divergence between document and top documents;
	 * - RERANKING_LINKS: local inter-connectivity and query terms found in anchor text;
	 * - RERANKING_RM: relevance model created from text in top documents.
	 **/
	static const int RERANKING_NONE = 0;
	static const int RERANKING_KLD = 1;
	static const int RERANKING_LINKS = 2;
	static const int RERANKING_BAYES = 3;

	/** Do we have to return search results in TREC format? **/
	bool trecFormat;

private:

	void initialize();

public:

	RankedQuery();

	RankedQuery(Index *index, const char *command, const char **modifiers,
			const char *body, uid_t userID, int memoryLimit);

	virtual ~RankedQuery();

	virtual bool parse();

	virtual bool getStatus(int *code, char *description);

	static bool isValidCommand(const char *command);

	virtual int getType();

	virtual ScoredExtent getResult(int i);

	/** Puts the next result line into the given buffer. **/
	virtual bool getNextLine(char *line);

	/** Prints basic SCORE FROM TO information into the given buffer. **/
	virtual void printResultLine(char *line, ScoredExtent sex);

	/**
	 * Sorts an array of "count" ScoredExtent instances by their score. If "inverted" is
	 * false, they are sorted by decreasing score, otherwise by increasing score.
	 **/
	static void sortResultsByScore(ScoredExtent *results, int count, bool inverted);

protected:

	/**
	 * This method defines the abstract query plan, such as
	 * FIRST_RETRIEVAL_STEP -> FEEDBACK -> SECOND_RETRIEVAL_STEP -> RERANKING.
	 **/
	virtual void processQuery();

	/**
	 * This method does the actual query processing work and need to be
	 * implemented by derived classes in order to do anything at all.
	 **/
	virtual void processCoreQuery() { count = 0; }

	/**
	 * These two functions are used to maintain the internal heap data structure.
	 * The heap is used to keep the best "count" container elements encountered
	 * so far in an almost-sorted order.
	 **/
	static void moveLastHeapNodeUp(ScoredExtent *heap, int heapSize);
	static void moveFirstHeapNodeDown(ScoredExtent *heap, int heapSize);

	/** Same as usual. **/
	virtual void processModifiers(const char **modifiers);

	/**
	 * Parses the given query, using the given default values to fill in
	 * container query and statistics query in case they have not been specified
	 * by the user. Calls parseScorers(char*) to parse the list of query terms.
	 * Returns true iff the query string was successfully parsed.
	 **/
	virtual bool parseQueryString(const char *queryString, const char *defaultContainer,
			const char *defaultStatisticsQuery, int memoryLimit);

	/**
	 * Parses the scorers given by the parameter and fills in the values
	 * of the "elementWeights" and "elementQueries" arrays. Calls createElementQuery(...)
	 * in order to create the individual GCL queries. Returns true if the parsing
	 * was successful.
	 **/
	virtual bool parseScorers(const char *scorers, int memoryLimit);

	/**
	 * Obtains global collection statistics: total size of collection, number of
	 * documents. Also obtains per-term statistics: collection frequency and
	 * document frequency.
	 **/
	virtual void getCorpusStatistics(
			offset *corpusSize, offset *documentCount, offset *scorerFreq, offset *scorerDF);

	/**
	 * Creates a GCLQuery instance from the query string given by "query". If
	 * a weight is specified in the query string, "weight" is set to that value,
	 * 1.0 otherwise.
	 **/
	virtual GCLQuery *createElementQuery(const char *query, double *weight, int memoryLimit);

	/**
	 * Returns a pointer to the first occurrence of "what" inside the string
	 * "where" or NULL if not present. Matching is case-sensitive or not,
	 * depending on the value of the third parameter.
	 **/
	static const char *findOutsideQuotationMarks(const char *string, const char *what, bool caseSensitive);

	/**
	 * Computes term weights based on their frequency in the whole collection
	 * (QAP-style term weighting) and puts the resulting numbers into the
	 * "elementWeights" array.
	 **/
	void computeTermCorpusWeights();

	/**
	 * Adds the given result candidate to the set of top-k search results.
	 * Restricts the new set back to the top-k results (maintaining the heap
	 * stored in "this->results" (k == "this->count").
	 **/
	void addToResultSet(ScoredExtent *candidate, int *resultCount);

	/**
	 * Takes a GCL expression. Returns an ExtentList instance if the given
	 * GCL expression could be parsed successfully. Otherwise, returns NULL.
	 * Object returned needs to be deleted by caller. Security restrictions
	 * applied to list will be the same as that of RankedQuery object
	 * (defined in this->VisibleExtents).
	 **/
	ExtentList *getListForGCLExpression(const char *expression);

	/** Returns a language model built from the top "docCount" results. **/
	LanguageModel *getLanguageModelFromTopResults(int docCount, bool withStemming);

	/**
	 * Performs pseudo-relevance feedback using a language model defined by the
	 * "docCount" top results. Adds "termCount" new terms to the list of scorers.
	 **/
	void feedback(int docCount, int termCount, bool withStemming);

	/**
	 * Use the top n documents in the result set to rerank the results according
	 * to their similarity to the top n documents. Similarity is measured by the
	 * Kullback-Leibler divergence from the language model defined by the top n
	 * documents and is subtracted from the original score of each document.
	 * The weight of the KLD score in the linear combination with the original
	 * score is specified via the parameter "weight".
	 * This includes the methods describes by Lavrenko and Croft (SIGIR 2001).
	 * "method" needs to have one of the values defined by RelevanceModel::METHOD_*.
	 **/
	void rerankResultsKLD(int docCount, double weight, int method);

	/**
	 * Use the top n documents in the result set to build a Naive Bayes classifier
	 * that is then used to adjust the document scores for all documents in the
	 * ranking.
	 **/
	void rerankResultsBayes(int docCount);

	/**
	 * Reranks the top n search results based on their local inter-connectivity,
	 * i.e., how they link to each other.
	 **/
	void rerankResultsLinks(int docCount);

	/**
	 * Only for experimental purposes: Performs KLD-based pseudo-relevance
	 * feedback on the top documents retrieved and reports statistics about
	 * query terms found in the top feedback candidate terms. This was used
	 * to come up with the statistics reported in the CIKM 2006 paper on
	 * KLD-based index pruning.
	 **/
	void analyzeKLD();

}; // end of class RankedQuery


REGISTER_QUERY_CLASS(RankedQuery, rank,
	"Runs a general ranked query on the current index.",
	"@rank provides the query processing infrastructure for most ranked queries.\n" \
	"It also can be used to access various ranking functions via a query modifier\n" \
	"(e.g., [bm25], [qap], ...).\n" \
	"The general query syntax (which is shared by most ranking commands) is as\n" \
	"follows:\n\n" \
	"  @rank[FUNCTION] WHAT by W_1 Q_1, ..., W_n Q_n [with weights from WHERE]\n\n" \
	"Here, WHAT is a GCL expression defining the targeted retrieval unit, e.g.,\n" \
	"\"<doc>\"..\"</doc>\". The optional GCL expression WHERE can be used to\n" \
	"obtain term statistics used in the ranking process from a different source.\n" \
	"This is useful in the context of XML retrieval, where the targeted retrieval\n" \
	"set might be too small to get reliable term statistics from it. For help on\n" \
	"GCL expressions, see \"@help gcl\".\n" \
	"The W_i are optional query term weights (assumed to be 1.0 if not present).\n" \
	"The Q_i are query terms, which, again, can be arbitrary GCL expressions.\n\n" \
	"Example:\n\n" \
	"  @rank[bm25][docid][count=5][id=42] \"<doc>\"..\"</doc>\" by \"information\", #2.0 \"retrieval\"\n" \
	"  42 3.809258 5822 5994 \"WSJ880314-0067\"\n" \
	"  42 2.849666 11400 11602 \"WSJ880314-0043\"\n" \
	"  42 2.804417 23817 24030 \"WSJ880314-0022\"\n" \
	"  42 2.721906 9687 9921 \"WSJ880314-0048\"\n" \
	"  42 2.580884 14406 14679 \"WSJ880314-0037\"\n" \
	"  @0-Ok. (1 ms)\n\n" \
	"Query modifiers supported (in addition to ranking function selection):\n" \
	"  GCL add (default: empty)\n" \
	"    makes the query processor find the first occurrence of the given GCL\n" \
	"    expression in each document returned; prints start and end of match\n" \
	"  GCL addget (default: empty)\n" \
	"    similar to [add], but returns the actual text instead of start/end pos'ns\n" \
	"  string id (default: 0)\n" \
	"    query ID string, used to distinguish between queries when run as batch job\n" \
	"  boolean trec (default: false)\n" \
	"    if set to true, forces the query processor to return results in TREC format\n" \
	"  string runid	(default: \"Wumpus\")\n" \
	"    only effective in TREC mode; sets the run ID in the TREC result lines\n" \
	"  string feedback (default: off)\n" \
	"    can be set to \"okapi\" or \"kld\" for Okapi-style or KLD feedback\n" \
	"  int fbterms (default: 15)\n" \
	"    sets the number of feedback terms to be added to the query\n" \
	"  int fbdocs (default: 15)\n" \
	"    sets the number of feedback documents to be used for pseudo-rel feedback\n" \
	"  string fbqrels (default: off)\n" \
	"    the filename of an explicit qrels file to be used for non-pseudo feedback\n" \
	"  float fbweight (default: 0.3)\n" \
	"    weight of expansion terms added to the original query\n" \
	"  bool fbreweight (default: false)\n" \
	"    makes the feedback method change the weights of the orig query terms\n" \
	"  bool fbstemming (default: false)\n"
	"    if true, then stem-equivalent terms are combined when doing the feedback step\n" \
	"  For further modifiers, see \"@help query\".\n"
)


/** Simple comparator function, to be used when sorting with qsort. **/
int extentScoreComparator(const void *a, const void *b);

/** Used to sort ScoredExtent objects in inverse order. **/
int invertedExtentScoreComparator(const void *a, const void *b);

/** Comparator function to sort ScoredExtent instances by offset. **/
int extentOffsetComparator(const void *a, const void *b);

/** Used when we have to sort offset values. **/
int offsetComparator(const void *a, const void *b);


#endif


