/**
 * Experimental retrieval functions are tested here. Always wear a helmet when
 * looking at the code found in this file or in experimental_query.cpp.
 *
 * author: Stefan Buettcher
 * created: 2007-03-26
 * changed: 2009-02-01
 **/


#ifndef __QUERY__EXPERIMENTAL_QUERY_H
#define __QUERY__EXPERIMENTAL_QUERY_H


#include "rankedquery.h"


class ExperimentalQuery : public RankedQuery {

private:

	/** Smoothing parameter in Bayesian smoothing with Dirichlet priors. **/
	double dirichletMu;

public:

	ExperimentalQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	ExperimentalQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~ExperimentalQuery();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

}; // end of class ExperimentalQuery


REGISTER_QUERY_CLASS(ExperimentalQuery, experimental,
	"Experimental relevance ranking.",
	"Probably produces incredibly low-quality search results. Don't use this." \
)
REGISTER_QUERY_ALIAS(experimental, exp)


#endif

