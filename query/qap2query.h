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
 * created: 2005-10-14
 * changed: 2009-02-01
 **/


#ifndef __QUERY__QAP2QUERY_H
#define __QUERY__QAP2QUERY_H


#include "bm25query.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


typedef struct {
	offset start;
	offset end;
	int who;
} Occurrence;


class QAP2Query : public BM25Query {

public:

	/** Dummy constructor. **/
	QAP2Query();

	/** Creates a new QAP2Query instance. **/
	QAP2Query(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	/** Creates a new QAP2Query instance. **/
	QAP2Query(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	virtual ~QAP2Query();

protected:

	virtual void processCoreQuery();

	virtual void printResultLine(char *target, ScoredExtent sex);

	ScoredExtent *getPassages(Occurrence *occ, int count, double avgdl);

}; // end of class QAP2Query


#endif



