/**
 * This is an implementation of a retrieval function based on divergence from
 * randomness, as defined in the paper:
 *
 * Amati, Rijsbergen, "Probabilistic models of information retrieval based
 * on measuring the divergence from randomness", ACM TOIS, 2002.
 *
 * The particular function implemented is I(F)B2.
 *
 * author: Stefan Buettcher
 * created: 2007-04-16
 * changed: 2009-02-01
 **/


#ifndef __QUERY__DIVERGENCE_QUERY_H
#define __QUERY__DIVERGENCE_QUERY_H


#include "rankedquery.h"


class DivergenceQuery : public RankedQuery {

public:

	DivergenceQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	DivergenceQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~DivergenceQuery();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

private:

	// The actual method used: METHOD_GB2 or METHOD_IFB2.
	int method;

	static const int METHOD_GB2 = 1;
	static const int METHOD_IFB2 = 2;
}; // end of class DivergenceQuery


REGISTER_QUERY_CLASS(DivergenceQuery, dfr,
	"Performs a ranked retrieval step based on divergence from randomness.",
	"The @dfr query command follows the standard syntax of most other ranked\n" \
	"queries (see \"@help rank\" for details). It ranks and retrieves a set of\n" \
	"documents according to the divergence from randomness model proposed by\n" \
	"Amati and Rijsbergen:\n\n" \
	"  G. Amati, C. van Rijsbergen, \"Probabilistic Models of Information Retrieval\n" \
	"  Based on Measuring the Divergence from Randomness\", ACM TOIS, 2002.\n\n" \
	"Two instantiations have been implemented: GB2 and I(F)B2.\n\n" \
	"Query modifiers supported:\n" \
	"  string method (default: ifb2)\n" \
	"    the exact method to be used: gb2 or ifb2\n" \
	"  For further modifiers, see \"@help rank\".\n"
)


#endif

