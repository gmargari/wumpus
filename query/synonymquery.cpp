/**
 * Copyright (C) 2010 Stefan Buettcher. All rights reserved.
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
 * Implementation of the SynonymQuery class.
 *
 * author: Stefan Buettcher
 * created: 2010-03-06
 * changed: 2010-03-06
 **/

#include <set>
#include <string>
#include "bm25query.h"
#include "synonymquery.h"
#include "../indexcache/indexcache.h"
#include "../misc/stringtokenizer.h"

SynonymQuery::SynonymQuery(Index *index, const char *command, const char **modifiers,
                           const char *body, uid_t userID, int memoryLimit) {
	this->index = index;
	visibleExtents = index->getVisibleExtents(userID, false);
	mustFreeVisibleExtentsInDestructor = true;
	this->queryString = normalizeString(duplicateString(body));
	processModifiers(modifiers);
}

SynonymQuery::~SynonymQuery() {
}

bool SynonymQuery::parse() {
	ok = true;

	std::vector<std::string> queryTerms;
	for (int i = 0; i < contextTerms.size(); ++i)
		queryTerms.push_back(contextTerms[i]);
	queryTerms.push_back(queryString);

	RankedQuery* originalRankedQuery = getRankedQuery(queryTerms, queryTerms);
	if (originalRankedQuery == NULL) {
		ok = false;
		return false;
	}

	const int docCount = originalRankedQuery->getCount();
	assert(docCount <= 100);
	if (verbose) {
		sprintf(scrap, "%d results found. Extracting top %d feedback terms.", docCount, feedbackTerms);
		addVerboseString(NULL, scrap);
	}

	offset docStarts[100], docEnds[100];
	for (int i = 0; i < docCount; ++i) {
		ScoredExtent sex = originalRankedQuery->getResult(i);
		docStarts[i] = sex.from;
		docEnds[i] = sex.to;
	}

	LanguageModel *originalLM = getLanguageModel(originalRankedQuery);

	assert(feedbackTerms <= 100);
	FeedbackScore scoredTerms[100];
	Feedback fb(index, false /*no stemming*/);
	fb.doFeedback(Feedback::FEEDBACK_OKAPI, docStarts, docEnds, docCount,
	              NULL, 0, scoredTerms, feedbackTerms);

	if (verbose) {
		std::vector<std::string> verbose_fbterms;
		for (int i = 0; i < feedbackTerms; ++i) {
			if (scoredTerms[i].score <= 0.0)
				break;
			sprintf(scrap, "%s:%.2f", scoredTerms[i].term, scoredTerms[i].score);
			verbose_fbterms.push_back(scrap);
		}
		addVerboseString(NULL, StringTokenizer::join(verbose_fbterms, ", ").c_str());
	}

	int cacheSize;
	LanguageModel *backgroundLM = (LanguageModel*)
		index->getCache()->getPointerToMiscDataFromCache("FEEDBACK_CACHE", &cacheSize);
	assert(backgroundLM != NULL);

	for (int i = 0; i < feedbackTerms; ++i) {
		if (scoredTerms[i].score <= 0.0)
			break;
		queryTerms[queryTerms.size() - 1] = scoredTerms[i].term;
		std::vector<std::string> scoringTerms;
		scoringTerms.push_back(scoredTerms[i].term);
		RankedQuery *thisRankedQuery = getRankedQuery(queryTerms, scoringTerms);
		assert(thisRankedQuery != NULL);
		if (thisRankedQuery->getCount() < 1) {
			delete thisRankedQuery;
			continue;
		}
		const double overlap = getOverlap(originalRankedQuery, thisRankedQuery);
		LanguageModel *thisLM = getLanguageModel(thisRankedQuery);
		const double kld =
			0.5 * (LanguageModel::getKLD(originalLM, thisLM, backgroundLM) +
			       LanguageModel::getKLD(thisLM, originalLM, backgroundLM));
		const double backgroundKLD =
			0.5 * (LanguageModel::kullbackLeiblerDivergence(originalLM, backgroundLM) +
			       LanguageModel::kullbackLeiblerDivergence(thisLM, backgroundLM));
		const double normalizedKLD =
			(1.0 - kld / backgroundKLD) * MIN(thisRankedQuery->getCount(), 10) / 10.0;
		const double weightedKLD = normalizedKLD * (1.0 + overlap);
		if (verbose) {
			sprintf(scrap, "overlap = %.2f, kld = %.4f, normalized_kld = %.4f, weighted_kld = %.4f",
			        overlap, kld, normalizedKLD, weightedKLD);
			addVerboseString(scoredTerms[i].term, scrap);
		}
		printf("%s: overlap = %.2f, kld = %.4f, normalized_kld = %.4f, weighted_kld = %.4f\n",
		       scoredTerms[i].term, overlap, kld, normalizedKLD, weightedKLD);
		delete thisRankedQuery;
		delete thisLM;
	}

	delete originalRankedQuery;
	delete originalLM;
	return true;
}

double SynonymQuery::getOverlap(RankedQuery *q1, RankedQuery *q2) {
	std::set<offset> r1;
	for (int i = 0; i < q1->getCount(); ++i)
		r1.insert(q1->getResult(i).from);
	int overlap = 0;
	for (int i = 0; i < q2->getCount(); ++i)
		if (r1.find(q2->getResult(i).from) != r1.end())
			++overlap;
	return overlap * 1.0 / MIN(q1->getCount(), q2->getCount());
}

LanguageModel * SynonymQuery::getLanguageModel(RankedQuery *r) {
	LanguageModel *lm = new LanguageModel(0, 0, false);
	for (int i = 0; i < r->getCount(); ++i) {
		ScoredExtent sex = r->getResult(i);
		LanguageModel tempLM(index, sex.from, sex.to, false);
		lm->addLanguageModel(&tempLM);
	}
	return lm;
}

RankedQuery * SynonymQuery::getRankedQuery(const std::vector<std::string> &retrievalTerms,
                                           const std::vector<std::string> &scoringTerms) {
	std::string booleanAND = StringTokenizer::join(retrievalTerms, "\"^\"");
	std::string container =
		std::string("(") + DOC_QUERY + std::string(")>(\"") + booleanAND + "\")";
	std::string scorers =
		std::string("\"") + StringTokenizer::join(scoringTerms, "\",\"") + "\"";
	std::string body = container + " by " + scorers;

	sprintf(scrap, "count=%d", feedbackDocs);
	if (verbose)
		addVerboseString(NULL, (std::string("Issuing query: @bm25[") + scrap + std::string("] ") + body).c_str());
	const char *modifiers[] = { scrap, NULL };
	BM25Query *bm25query =
		new BM25Query(index, "bm25", modifiers, body.c_str(), visibleExtents, -1);
	if (!bm25query->parse()) {
		delete bm25query;
		return NULL;
	} else {
		return bm25query;
	}
}

bool SynonymQuery::getNextLine(char *line) {
  if (verboseText != NULL) {
		strcpy(line, verboseText);
		free(verboseText);
		verboseText = NULL;
		return true;
	}
	if (resultLines.empty())
		return false;
	strcpy(line, resultLines.front().c_str());
	resultLines.pop();
	return true;
}

bool SynonymQuery::getStatus(int *code, char *description) {
	if (syntaxErrorDetected)
		return getStatusSyntaxError(code, description);
	else
		return getStatusOk(code, description);
}

int SynonymQuery::getType() {
	return QUERY_TYPE_MISC;
}

void SynonymQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
  feedbackTerms = getModifierInt(modifiers, "fbterms", 20);
	feedbackTerms = MIN(100, MAX(feedbackTerms, 1));
	feedbackDocs = getModifierInt(modifiers, "fbdocs", 15);
	feedbackDocs = MIN(100, MAX(feedbackDocs, 1));
	char* context = getModifierString(modifiers, "context", NULL);
	if (context != NULL) {
		StringTokenizer::split(context, ",", &contextTerms);
		free(context);
	}
}

