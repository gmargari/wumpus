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
 * Definition of the Feedback class. Feedback is used to integrate pseudo-relevance
 * feedback into the query processing. Several pseudo-relevance feedback schemes are
 * supported.
 *
 * author: Stefan Buettcher
 * created: 2005-06-10
 * changed: 2007-07-02
 **/


#ifndef __FEEDBACK__FEEDBACK_H
#define __FEEDBACK__FEEDBACK_H


#include "../index/index_types.h"
#include "language_model.h"


/**
 * This structure describes an output term (feedback candidate term) that is
 * returned by the feedback process.
 **/
typedef struct {

	/** The term whose feedback score is given here. **/
	char term[MAX_TOKEN_LENGTH * 2];

	/** The feedback score itself. **/
	double score;

	/**
	 * The retrieval weight that we would assign this term if it were added to
	 * the query.
	 **/
	double weight;

} FeedbackScore;


class Index;
class CompactIndex;


class Feedback {

public:

	/**
	 * We bound the number of documents that can be examined in order to avoid
	 * messy cases for Billerbeck's term selection value (factorials and powers
	 * becoming too large).
	 **/
	static const int FEEDBACK_MAX_DOCUMENT_COUNT = 100;

	static const int MAX_TF_CACHE_TERMCOUNT = 2000000;

	/** Feedback mode selectors. **/
	static const int FEEDBACK_NONE = 0;
	static const int FEEDBACK_OKAPI = 1;
	static const int FEEDBACK_KLD = 2;
	static const int FEEDBACK_BILLERBECK = 3;

private:

	/** Index instance to be used to obtain term statistics. **/
	Index *index;

	/** Tells us whether we want to use a stemmed or an unstemmed language model. **/
	bool withStemming;

	/** Some collection statistics. **/
	double corpusSize, documentCount;

	/** This language model is used to cache collection-wide term frequencies. **/
	LanguageModel *collectionModel;

public:

	/**
	 * Creates a new Feedback object that used the given Index instance to obtain
	 * document text and term statistics. "withStemming" is used to specify whether
	 * the feedback mechanism automatically applies stemming to the candidate terms.
	 **/
	Feedback(Index *index, bool withStemming);

	/** Class destructor. **/
	~Feedback();

	/**
	 * Performs a pseudo-relevance feedback operation, using the feedback algorithm
	 * specified by "feedbackMode" and the pseudo-relevant documents given by
	 * the "doc*" arguments. The output terms are sorted by decreasing score.
	 **/
	void doFeedback(int feedbackMode, offset *docStarts, offset *docEnds, int docCount,
			char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int termCount);

private:

	void doOkapiFeedback(LanguageModel *feedbackModel, double documentCount,
			char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int termCount);

	void doBillerbeckFeedback(LanguageModel *feedbackModel, double documentCount,
			char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int termCount);

	void doKullbackLeiblerFeedback(LanguageModel *feedbackModel, double documentCount,
			char **queryTerms, int queryTermCount, FeedbackScore *feedbackTerms, int termCount);

	/** Returns the number of documents containing the given term. **/
	double getDocumentsContaining(char *term);

	/** Returns the number of occurrences of the given term within the whole collection. **/
	double getTermFrequency(char *term);

	/**
	 * Builds a LanguageModel instance, containing term frequencies and per-term
	 * document counts for every term found in at least one of the given documents.
	 * The language model will be exclusively based on the text found inside the
	 * given documents.
	 **/
	LanguageModel *buildFeedbackModel(offset *docStarts, offset *docEnds, int docCount);

	/**
	 * Fills the given term's collection frequency and document frequency into
	 * "tf" and "df". If the term cannot be found in the language model, fills
	 * them with zero.
	 **/
	void getTermInfo(char *term, offset *tf, offset *df);

}; // end of class Feedback


#endif


