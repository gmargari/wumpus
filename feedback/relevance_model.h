/**
 * author: Stefan Buettcher
 * created: 2006-09-27
 * changed: 2006-09-27
 **/


#ifndef __FEEDBACK__RELEVANCE_MODEL_H
#define __FEEDBACK__RELEVANCE_MODEL_H


#include "language_model.h"

class Index;


class RelevanceModel : public LanguageModel {

private:

	static const int MAX_DOC_COUNT = 100;

	Index *index;

public:

	static const int METHOD_CONCAT = 1;
	static const int METHOD_WEIGHTED = 2;
	static const int METHOD_LAVRENKO_1 = 3;
	static const int METHOD_LAVRENKO_2 = 4;

public:

	/**
	 * Constructs a new relevance model (a stemmed unigram language model) from the
	 * given top documents and the given query.
	 * Depending on the value of "method", different methods to construct the language
	 * model may be chosen.
	 **/
	RelevanceModel(Index *index, offset *docStarts, offset *docEnds, double *docScores,
			           int docCount, char **queryTerms, int termCount, int method);

	~RelevanceModel();

private:

	/** Builds a language model buy concatenating the text found in the given documents. **/
	void buildModelConcat(LanguageModel **docModels, int docCount);

	/**
	 * Builds a language model by constructing a linear combination of the language models
	 * defined by the input documents. The weights in the linear combination are given
	 * by the "weights" parameter.
	 **/
	void buildModelWeighted(LanguageModel **docModels, double *weights, int docCount);

	/** Constructs a relevance model according to method 1 from Lavrenko's 2001 SIGIR paper. **/
	void buildModelLavrenko1(
			LanguageModel **docModels, double *weights, int docCount, char **queryTerms, int termCount);

	/** Constructs a relevance model according to method 2 from Lavrenko's 2001 SIGIR paper. **/
	void buildModelLavrenko2(
			LanguageModel **docModels, double *weights, int docCount, char **queryTerms, int termCount);

}; // end of class RelevanceModel


#endif


