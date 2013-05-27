/**
 * This is an implementation of the BM25F retrieval function,
 * as described in the following publications:
 *
 * H. Zaragoza, N. Craswell, M. Taylor, S. Saria, and S. Robertson,
 * "Microsoft Cambridge at TREC 13: Web and Hard tracks", Proceedings
 * of the 13th Text REtrieval Conference, Gaithersburg, USA, 2004.
 *
 * Nick Craswell, Hugo Zaragoza, and Stephen Robertson,
 * "Microsoft Cambridge at TREC 14: Enterprise track", Proceedings of the
 * 14th Text REtrieval Conference, Gaithersburg, USA, 2005.
 *
 * author: Stefan Buettcher
 * created: 2008-07-03
 * changed: 2009-02-01
 **/


#ifndef __QUERY__BM25F_QUERY_H
#define __QUERY__BM25F_QUERY_H


#include "rankedquery.h"


class BM25FQuery : public RankedQuery {

public:

	BM25FQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	BM25FQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~BM25FQuery();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/**
	 * Standard modifier processing routine, based on
	 * RankedQuery::processModifiers.
	 **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

	/**
	 * BM25 parameters. k1 has the standard meaning. b_1 and b_2 are the
	 * length normalization parameters for the field and the remainder of
	 * the document, respectively.
	 **/
	double k1, b1, b2;

	/** The weight of the special field in the BM25F formula. **/
	double w;

	/**
	 * The list of index extents matching the field query. For example, if we
	 * have "[field=title]", then this list will contain all matches for the GCL
	 * expression "<title>".."</title>".
	 **/
	ExtentList *fieldList;

}; // end of class DivergenceQuery


REGISTER_QUERY_CLASS(BM25FQuery, bm25f,
	"Performs ranked retrieval step according to BM25F (BM25 + weighted fields).",
	"The @bm25f query command follows the standard syntax of most other ranked\n" \
	"queries (see \"@help rank\" for details). It ranks and retrieves a set of\n" \
	"documents according to their BM25F scores. For an introduction to BM25F,\n" \
	"see Zaragoza et al., \"Microsoft Cambridge at TREC 13: Web and Hard\n" \
	"tracks\", TREC 2004.\n" \
	"This implementation of BM25F uses exactly two fields per document.\n" \
	"Field #1 can be set through the \"field\" parameter. Field #2 is the\n" \
	"remainder of the document. If the field appears multiple times in the\n" \
	"same document, only the first occurrence is used for scoring.\n\n" \
	"Query modifiers supported:\n" \
	"  string field (default: title)\n" \
	"    the special field; this will automatically be translated into a GCL\n" \
	"    query of the form \"<field>\"..\"</field>\"\n" \
	"  float w (default: 2.0)\n" \
	"    the weight of the field (the remainder always has weight 1.0)\n" \
	"  float k1 (default: 1.2)\n" \
	"    BM25 TF fan-out parameter\n" \
	"  float b1 (default: 0.75)\n" \
	"    BM25 length normalization parameter for the field\n" \
	"  float b2 (default: 0.75)\n" \
	"    BM25 length normalization parameter for the remainder of the document\n" \
	"  For further modifiers, see \"@help rank\".\n"
)


#endif

