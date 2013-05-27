/**
 * This is an implementation of a language-model-based ranking function, as
 * defined for instance in Zhai's 2001 SIGIR paper.
 *
 * author: Stefan Buettcher
 * created: 2006-09-06
 * changed: 2009-02-01
 **/


#ifndef __QUERY__LANGUAGEMODEL_QUERY_H
#define __QUERY__LANGUAGEMODEL_QUERY_H


#include "rankedquery.h"


class LanguageModelQuery : public RankedQuery {

private:

	/** Possible smoothing methods supported. **/
	static const int SMOOTHING_DIRICHLET = 1;
	static const int SMOOTHING_JELINEK = 2;

	/** Smoothing method chosen by user. **/
	int smoothingMethod;

	/** Smoothing parameter in Bayesian smoothing with Dirichlet priors. **/
	double dirichletMu;
	static const double DEFAULT_MU = 2000;

	/** Smoothing parameter in Jelinek-Mercer smoothing (lin. interpolation). **/
	double jelinekLambda;
	static const double DEFAULT_LAMBDA = 0.2;

public:

	LanguageModelQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	LanguageModelQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~LanguageModelQuery();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

}; // end of class LanguageModelQuery


REGISTER_QUERY_CLASS(LanguageModelQuery, lm,
	"Performs a ranked retrieval step based on language modeling.",
	"The @lm query command follows the standard syntax of most other ranked\n" \
	"queries (see \"@help rank\" for details). It ranks and retrieves a set of\n" \
	"documents according to the probability that a given document has created the\n" \
	"query (language modeling approach). The exact method is based on Bayesian\n" \
	"smoothing with Dirichlet priors, as suggested by Zhai and Lafferty,\n" \
	"\"A study of smoothing methods for language models applied to information\n" \
	"\"retrieval\". ACM TOIS, 22(2), 179-214, 2004.\n\n" \
	"Query modifiers supported:\n" \
	"  float mu (default: 2000)\n" \
	"    model-specific smoothing parameter (used for Dirichlet smoothing)\n" \
	"  float lambda (default: 0.2)\n" \
	"    model-specific smoothing parameter (used for Jelinek-Mercer smoothing)\n" \
	"  string smoothing (default: dirichlet)\n" \
	"    set to \"dirichlet\", \"jelinek-mercer\"/\"jm\", or \"none\" for no smoothing\n" \
	"  For further modifiers, see \"@help rank\".\n"
)
REGISTER_QUERY_ALIAS(lm, lmd)


#endif

