/**
 * This is an implementation of a language-model-based ranking function, as
 * defined in the SIGIR 1998 paper by Ponte and Croft.
 *
 * author: Stefan Buettcher
 * created: 2006-01-23
 * changed: 2009-02-01
 **/


#ifndef __QUERY__PONTE_CROFT_H
#define __QUERY__PONTE_CROFT_H


#include "rankedquery.h"


class PonteCroft : public RankedQuery {

public:

	PonteCroft(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	PonteCroft(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~PonteCroft();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

}; // end of class PonteCroft


#endif

