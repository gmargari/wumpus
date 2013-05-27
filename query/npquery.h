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
 * author: Stefan Buettcher
 * created: 2006-07-09
 * changed: 2009-02-01
 **/


#ifndef __QUERY__NPQUERY_H
#define __QUERY__NPQUERY_H


#include "query.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


class NPQuery : public RankedQuery {

public:

	/** These are the standard Robertson/Walker parameters for BM25. **/
	static const double DEFAULT_K1 = 1.2;
	static const double DEFAULT_B = 0.75;
	static const double DEFAULT_DECAY = 1.5;

	/** Our BM25 implementation will not score containers that are smaller than this. **/
	static const int MIN_OKAPI_CONTAINER_SIZE = 4;

protected:

	/** BM25 parameters. **/
	double k1, b;

	/** Exponent for proximity stuff. **/
	double decay;

public:

	/** Stupid default constructor. **/
	NPQuery() { }

	NPQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	NPQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	virtual ~NPQuery();

protected:

	/** This method does the actual work for the constructors. **/
	virtual void initialize(Index *index, const char *command, const char **modifiers,
			const char *body, VisibleExtents *visibleExtents, int memoryLimit);

	/** Standard modifier processing routine, based on RankedQuery::processModifiers. **/
	virtual void processModifiers(const char **modifiers);

	/** The actual query processing. **/
	virtual void processCoreQuery();

}; // end of class NPQuery


#endif


