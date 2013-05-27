/**
 * This is an implementation of a language-model-based ranking function, as
 * defined for instance in Zhai's 2001 SIGIR paper.
 *
 * author: Stefan Buettcher
 * created: 2007-03-14
 * changed: 2009-02-01
 **/


#ifndef __QUERY__VECTORSPACE_QUERY_H
#define __QUERY__VECTORSPACE_QUERY_H


#include "rankedquery.h"


struct VectorSpaceDocLen {
	offset docStart;
	double docLen;
};


class VectorSpaceQuery : public RankedQuery {

private:

	/** Indicates whether document length normalization contains IDF component. **/
	bool useIDF;

	/** Should document scores be normalized (cosine measure)? **/
	bool rawScores;

	/** Indicates that linear TF scores should be used instead of logarithmic ones. **/
	bool linearTF;

	/** Array containing the length of each document vector in the collection. **/
	VectorSpaceDocLen *docLens;

	/** Number of documents in "docLens" array. **/
	int docCnt;

	/**
	 * The data in "docLens" are obtained through mmap. "fd" is the handle to the
	 * file containing those data.
	 **/
	int fd;

public:

	VectorSpaceQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	VectorSpaceQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~VectorSpaceQuery();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

private:

	/** Returns the length of the document vector for the given document start offset. **/
	double getVectorLength(offset documentStart);

}; // end of class VectorSpaceQuery


REGISTER_QUERY_CLASS(VectorSpaceQuery, vectorspace,
	"Performs ranked retrieval based on the vector space model.",
	"Ranks a set of documents based on their vector-space similarity to the\n" \
	"given query. Query syntax is the same as for all other ranked queries\n" \
	"(see @help rank for details).\n" \
	"The actual function implemented is that used by Buckley et al.,\n" \
	"\"Automatic Query Expansion Using SMART: TREC 3\", TREC 1994.\n\n" \
	"Vector space retrieval is a bit nasty, in that it requires access to\n" \
	"the length of each document vector. This information can be computed\n" \
	"from an existing index file by using handyman with parameter\n" \
	"BUILD_DOCUMENT_LENGTH_VECTOR. Put the resulting file into the Wumpus\n" \
	"database directory, with filename \"doclens.tf\" or \"doclens.tfidf\"\n" \
	"before running the query (filename depends on whether [noidf] is present).\n" \
	"a vector space query.\n\n" \
	"Query modifiers supported:\n" \
	"  boolean noidf (default: false)\n" \
	"    computes document vector withouts taking IDF component into account;\n" \
	"    note that IDF will still be used for the query vector\n" \
	"  boolean raw (default: false)\n" \
	"    if set to true, makes the query processor report unnormalized scores\n" \
	"    (i.e., without dividing by length of document vector)\n" \
	"  boolean linear_tf (default: false)\n" \
	"    uses a linear TF function instead of the default logarithmic one (if you\n" \
	"    set this flag, make sure the doc length file is created with --linear_tf)\n" \
	"  For further modifiers, see \"@help rank\".\n"
)
REGISTER_QUERY_ALIAS(vectorspace, vsm)


#endif

