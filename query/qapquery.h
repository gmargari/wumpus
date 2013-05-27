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
 * Definition of the QAPQuery class. QAPQuery implements Clarke's QAP
 * passage scoring mechanism.
 *
 * author: Stefan Buettcher
 * created: 2004-10-23
 * changed: 2009-02-01
 **/


#ifndef __QUERY__QAPQUERY_H
#define __QUERY__QAPQUERY_H


#include "query.h"
#include "gclquery.h"
#include "rankedquery.h"
#include "../index/index.h"


class QAPQuery : public RankedQuery {

protected:

	/**
	 * The parameter k1 can be used to set the impact of multiple occurrences of the
	 * same term within a passage. k1 = 0 corresponds to a boolean decision, whereas
	 * k1 = infinity defines a naive TF component. Okapi BM25 uses k1 = 1.2; we have
	 * had good experience with k1 around 1.0 for QAP.
	 **/
	double k1;
	static const double DEFAULT_K1 = 0.0;

	/** IDF weights of the query terms. **/
	double elementCorpusWeights[MAX_SCORER_COUNT];

public:

	QAPQuery();

	/**
	 * Creates a new QAPQuery instance. If "container == NULL", we do not score
	 * containers but return a list of passages directly.
	 **/
	QAPQuery(Index *index, const char *command, const char **modifiers, const char *body,
			uid_t userID, int memoryLimit);

	QAPQuery(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

	virtual ~QAPQuery();

	virtual bool parse();

	virtual bool getNextLine(char *line);

	static offset quickSelect(offset *array, int rank, int length);

protected:

	virtual void processCoreQuery();

	virtual void processModifiers(const char **modifiers);

	virtual void initialize(Index *index, const char *command, const char **modifiers, const char *body,
			VisibleExtents *visibleExtents, int memoryLimit);

}; // end of class QAPQuery


REGISTER_QUERY_CLASS(QAPQuery, qap,
	"Performs MultiText QAP passage-based relevance ranking.",
	"QAP follows the standard syntax of most other ranked queries\n" \
	"(see \"@help rank\" for details). Its output format is slightly different\n" \
	"from the usual @rank output format, because it also reports the top-ranking\n" \
	"passage from each matching document.\n\n"
	"Example:\n\n" \
	"  @qap[docid][3] \"<doc>\"..\"</doc>\" by \"information\", \"retrieval\"\n" \
	"  0 19.678873 223374 223895 223704 223705 \"WSJ880712-0023\"\n" \
	"  0 19.678873 203238 203962 203536 203537 \"WSJ880712-0061\"\n" \
	"  0 19.678873 217261 217797 217679 217680 \"WSJ880712-0033\"\n" \
	"  @0-Ok. (4 ms)\n\n" \
	"Query modifiers supported:\n" \
	"  None.\n" \
	"  For further modifiers, see \"@help rank\".\n"
)


#endif


