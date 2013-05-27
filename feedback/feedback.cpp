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
 * Implementation of the Feedback class that is responsible for pseudo-
 * relevance feedback.
 *
 * author: Stefan Buettcher
 * created: 2005-06-10
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <math.h>
#include <string.h>
#include <set>
#include <string>
#include "feedback.h"
#include "../index/compactindex.h"
#include "../index/index.h"
#include "../index/lexicon.h"
#include "../indexcache/indexcache.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"
#include "../query/countquery.h"
#include "../query/getquery.h"
#include "../query/query.h"
#include "../stemming/stemmer.h"
#include "../terabyte/terabyte.h"


static const char * LOG_ID = "Feedback";


Feedback::Feedback(Index *index, bool withStemming) {
	char line[1024];
	const char *MODIFIERS[2] = { "size", NULL };

	this->index = index;
	this->withStemming = withStemming;
	this->corpusSize = 1;
	this->documentCount = 1;

	Query *documentCountQuery =
		new CountQuery(index, "count", EMPTY_MODIFIERS, DOC_QUERY, Index::GOD, -1);
	if (documentCountQuery->parse())
		if (documentCountQuery->getNextLine(line))
			sscanf(line, "%lf", &documentCount);
	delete documentCountQuery;

	Query *corpusSizeQuery =
		new CountQuery(index, "count", MODIFIERS, DOC_QUERY, Index::GOD, -1);
	if (corpusSizeQuery->parse())
		if (corpusSizeQuery->getNextLine(line))
			sscanf(line, "%lf", &corpusSize);
	delete corpusSizeQuery;

	collectionModel = NULL;
	if (index != NULL)
		if (index->getCache() != NULL) {
			int cacheSize;
			collectionModel = (LanguageModel*)
				index->getCache()->getPointerToMiscDataFromCache("FEEDBACK_CACHE", &cacheSize);
			if (collectionModel == NULL) {
				char staticLmFile[MAX_CONFIG_VALUE_LENGTH];
				if (getConfigurationValue("STATIC_LANGUAGE_MODEL", staticLmFile))
					collectionModel = new LanguageModel(staticLmFile);
				else
					collectionModel = new LanguageModel(corpusSize, documentCount, withStemming);
				index->getCache()->addMiscDataToCache(
						"FEEDBACK_CACHE", collectionModel, sizeof(LanguageModel), true);
				collectionModel = (LanguageModel*)
					index->getCache()->getPointerToMiscDataFromCache("FEEDBACK_CACHE", &cacheSize);
				assert(collectionModel != NULL);
			}
		}
} // end of Feedback(Index*, bool)


Feedback::~Feedback() {
} // end of ~Feedback()


LanguageModel * Feedback::buildFeedbackModel(offset *docStarts, offset *docEnds, int docCount) {
	LanguageModel *result = new LanguageModel(0, 0, withStemming);
	for (int i = 0; i < docCount; i++) {
		LanguageModel *documentModel =
			new LanguageModel(index, docStarts[i], docEnds[i], withStemming);
		result->addLanguageModel(documentModel);
		delete documentModel;
	}
	return result;
} // end of buildFeedbackModel(offset*, offset*, int)


double Feedback::getDocumentsContaining(char *term) {
	char t[1024], resultLine[1024];
	if ((withStemming) && (strchr(term, '$') == NULL))
		sprintf(t, "\"$%s\"", term);
	else
		sprintf(t, "\"%s\"", term);
	CountQuery *q =
		new CountQuery(index, "documentsContaining", EMPTY_MODIFIERS, t, Index::GOD, -1);
	bool status = q->parse();
	assert(status == true);
	q->getNextLine(resultLine);
	delete q;
	double result = 1.0;
	sscanf(resultLine, "%lf", &result);
	return result;
} // end of getDocumentsContaining(char*)


double Feedback::getTermFrequency(char *term) {
	char t[1024], resultLine[1024];
	if ((withStemming) && (strchr(term, '$') == NULL))
		sprintf(t, "\"$%s\"", term);
	else
		sprintf(t, "\"%s\"", term);
	CountQuery *q =
		new CountQuery(index, "count", EMPTY_MODIFIERS, t, Index::GOD, -1);
	bool status = q->parse();
	if (!status) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Unable to parse @count query: %s", t);
		log(LOG_ERROR, LOG_ID, msg);
		assert(status);
	}
	q->getNextLine(resultLine);
	delete q;
	double result = 1.0;
	sscanf(resultLine, "%lf", &result);
	return result;
} // end of getTermFrequency(char*)


void Feedback::getTermInfo(char *term, offset *tf, offset *df) {
	*tf = *df = 0;
	if (collectionModel != NULL)
		collectionModel->getTermInfo(term, tf, df);
#if 0
	if (*df == 0) {
		*tf = (offset)(getTermFrequency(terms[i].term) + 0.1);
		if (*tf < 1)
			*tf = 1;
		*df = (offset)(getDocumentsContaining(terms[i].term) + 0.1);
		if (*df < 1)
			*df = 1;
		if (collectionModel != NULL)
			collectionModel->addTerm(terms[i].term, *tf, *df);
	}
#endif
} // end of getTermInfo(char*, offset*, offset*)


static int compareByScore(const void *a, const void *b) {
	FeedbackScore *x = (FeedbackScore*)a;
	FeedbackScore *y = (FeedbackScore*)b;
	if (x->score > y->score)
		return -1;
	else if (x->score < y->score)
		return +1;
	else
		return 0;
} // end of compareByScore(const void*, const void*)


void Feedback::doFeedback(int feedbackMode, offset *docStarts, offset *docEnds, int docCount,
		char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int feedbackTermCount) {
	feedbackTerms[0].score = -1.0;
	for (int i = 0; i < feedbackTermCount; i++)
		feedbackTerms[i].term[0] = 0;

	if ((docCount < 1) || (documentCount < 1)) {
		log(LOG_DEBUG, LOG_ID,
				"Unable to perform pseudo-relevance feedback on less than 1 document.");
		return;
	}
	if (docCount > FEEDBACK_MAX_DOCUMENT_COUNT)
		docCount = FEEDBACK_MAX_DOCUMENT_COUNT;

	// build a language model containing all terms found in the "docCount"
	// documents provided by the caller
	LanguageModel *feedbackModel = buildFeedbackModel(docStarts, docEnds, docCount);
	int termCount = feedbackModel->termSlotsUsed;
	if (termCount <= 1) {
		delete feedbackModel;
		return;
	}	

	switch (feedbackMode) {
		case FEEDBACK_OKAPI:
			doOkapiFeedback(feedbackModel, documentCount,
					queryTerms, queryTermCount, feedbackTerms, feedbackTermCount);
			break;
		case FEEDBACK_BILLERBECK:
			doBillerbeckFeedback(feedbackModel, documentCount,
					queryTerms, queryTermCount, feedbackTerms, feedbackTermCount);
		case FEEDBACK_KLD:
			doKullbackLeiblerFeedback(feedbackModel, documentCount,
					queryTerms, queryTermCount, feedbackTerms, feedbackTermCount);
			break;
	}
} // end of doFeedback(...)


void Feedback::doOkapiFeedback(LanguageModel *feedbackModel, double documentCount,
		char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int feedbackTermCount) {
	int termCount = feedbackModel->termSlotsUsed;
	assert(termCount > 0);
	FeedbackScore *terms = typed_malloc(FeedbackScore, termCount);

	// build a set of original query terms, for fast lookup
	std::set<std::string> queryTermSet;
	for (int i = 0; i < queryTermCount; i++)
		queryTermSet.insert(queryTerms[i]);

	// compute term scores according to Robertson's selection function
	double N = collectionModel->documentCount;
	int R = (int)(feedbackModel->documentCount + 0.1); // * 2 + 1;
	for (int i = 0; i < termCount; i++) {
		if (feedbackModel->terms[i].documentCount <= 1) {
			terms[i].score = -1;
			continue;
		}
		strcpy(terms[i].term, feedbackModel->terms[i].term);
		offset tf = 0, df = 0;
		getTermInfo(terms[i].term, &tf, &df);

		terms[i].score = 1E-9;
		if (df <= 0)
			continue;

		int r = feedbackModel->terms[i].documentCount;
		if (queryTermSet.find(terms[i].term) != queryTermSet.end()) {
			// special treatment for original query terms: donate "R" virtual documents
//			r = MIN(df, r + R / 2);
		}
		// compute Robertson's Selection Value
		terms[i].score = r * log(N / df);
		terms[i].weight = log(((r + 0.5) * (N - df - R + r + 0.5)) / ((R - r + 0.5) * (df - r + 0.5)));
		terms[i].weight /= log(N / df);
	}

	// sort all terms by their feedback score
	qsort(terms, termCount, sizeof(FeedbackScore), compareByScore);

	// cleanup the TF cache to make sure it does not consume too much memory
	if (collectionModel != NULL)
		collectionModel->restrictToMostFrequent(MAX_TF_CACHE_TERMCOUNT);
	memcpy(feedbackTerms, terms, MIN(termCount, feedbackTermCount) * sizeof(FeedbackScore));
	if (termCount < feedbackTermCount)
		feedbackTerms[termCount].score = -1;
	free(terms);
} // end of doOkapiFeedback(...)


void Feedback::doBillerbeckFeedback(LanguageModel *feedbackModel, double documentCount,
		char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int feedbackTermCount) {
	int termCount = feedbackModel->termSlotsUsed;
	assert(termCount > 0);
	FeedbackScore *terms = typed_malloc(FeedbackScore, termCount);

	// build a set of original query terms, for fast lookup
	std::set<std::string> queryTermSet;
	for (int i = 0; i < queryTermCount; i++)
		queryTermSet.insert(queryTerms[i]);

	// compute term scores according to Billerbeck's selection function
	double N = collectionModel->documentCount;
	int R = (int)(feedbackModel->documentCount + 0.1);
	for (int i = 0; i < termCount; i++) {
		if (feedbackModel->terms[i].documentCount <= 1) {
			terms[i].score = -1;
			continue;
		}
		strcpy(terms[i].term, feedbackModel->terms[i].term);
		offset tf = 0, df = 0;
		getTermInfo(terms[i].term, &tf, &df);

		terms[i].score = -1;
		if (df <= 0)
			continue;

		int r = feedbackModel->terms[i].documentCount;

		terms[i].score = 1 - pow(df / N, r) * pow(1 - df / N, R - r) * n_choose_k(R, r);
		terms[i].weight = log(((r + 0.5) * (N - df - R + r + 0.5)) / ((R - r + 0.5) * (df - r + 0.5)));
		terms[i].weight /= log(N / df);

		if (terms[i].score < 1E-9) {
			terms[i].score = 1E-9;
			terms[i].weight = 1E-9;
		}
	} // end for (int i = 0; i < termCount; i++)

	// sort all terms by their feedback score
	qsort(terms, termCount, sizeof(FeedbackScore), compareByScore);

	// cleanup the TF cache to make sure it does not consume too much memory
	if (collectionModel != NULL)
		collectionModel->restrictToMostFrequent(MAX_TF_CACHE_TERMCOUNT);
	memcpy(feedbackTerms, terms, MIN(termCount, feedbackTermCount) * sizeof(FeedbackScore));
	if (termCount < feedbackTermCount)
		feedbackTerms[termCount].score = -1;
	free(terms);
} // end of doBillerbeckFeedback(...)


void Feedback::doKullbackLeiblerFeedback(LanguageModel *feedbackModel, double documentCount,
		char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int feedbackTermCount) {
	int termCount = feedbackModel->termSlotsUsed;
	assert(termCount > 0);
	FeedbackScore *terms = typed_malloc(FeedbackScore, termCount);

	// build a set of original query terms, for fast lookup
	std::set<std::string> queryTermSet;
	for (int i = 0; i < queryTermCount; i++)
		queryTermSet.insert(queryTerms[i]);

	// compute term scores according to Billerbeck's selection function
	double N = collectionModel->documentCount;
	double C = collectionModel->corpusSize;
	int R = (int)(feedbackModel->documentCount + 0.1);
	for (int i = 0; i < termCount; i++) {
		if (feedbackModel->terms[i].documentCount <= 1) {
			terms[i].score = -1;
			continue;
		}
		strcpy(terms[i].term, feedbackModel->terms[i].term);
		offset tf = 0, df = 0;
		getTermInfo(terms[i].term, &tf, &df);

		terms[i].score = -1;
		if (df <= 0)
			continue;

		int r = feedbackModel->terms[i].documentCount;
		double p = feedbackModel->terms[i].termFrequency / feedbackModel->corpusSize;
		double q = tf / C;
		terms[i].score = (p > q ? p * log(p / q) : 0);
		terms[i].weight = log(((r + 0.5) * (N - df - R + r + 0.5)) / ((R - r + 0.5) * (df - r + 0.5)));
		terms[i].weight /= log(N / df);

		if (terms[i].score < 1E-9) {
			terms[i].score = 1E-9;
			terms[i].weight = 1E-9;
		}
	} // end for (int i = 0; i < termCount; i++)

	// sort all terms by their feedback score
	qsort(terms, termCount, sizeof(FeedbackScore), compareByScore);

	// cleanup the TF cache to make sure it does not consume too much memory
	if (collectionModel != NULL)
		collectionModel->restrictToMostFrequent(MAX_TF_CACHE_TERMCOUNT);
	memcpy(feedbackTerms, terms, MIN(termCount, feedbackTermCount) * sizeof(FeedbackScore));
	if (termCount < feedbackTermCount)
		feedbackTerms[termCount].score = -1;
	free(terms);
} // end of doKullbackLeiblerFeedback(...)


