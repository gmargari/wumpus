/**
 * The IncompleteLanguageModel class is used to specify a unigram language model
 * with incomplete term frequency information. It sits on top of another
 * language model that is used to infer (or, rather, approximate) unknown term
 * frequency information from global statistics.
 * IncompleteLanguageModel is, for example, used to approximate the language model
 * of a document, given its k-term surrogate.
 *
 * author: Stefan Buettcher
 * created: 2006-08-27
 * changed: 2006-08-27
 **/


#ifndef __FEEDBACK__INCOMPLETE_LANGUAGE_MODEL_H
#define __FEEDBACK__INCOMPLETE_LANGUAGE_MODEL_H


#include "abstract_language_model.h"


class IncompleteLanguageModel : public AbstractLanguageModel {

private:

	/** Tells us whether the background model has to be deleted in the destructor. **/
	bool mustDeleteBackgroundModel;

	/** Background model used to approximate term frequencies for unknown terms. **/
	AbstractLanguageModel *backgroundModel;

	/** Pointer to a hashtable for TF probabilities. **/
	void *tfProbabilities;

	/**
	 * Portion of the probability space covered by known terms. This value is
	 * initialized to 0 and updated whenever setTermProbability is called.
	 **/
	double spaceCovered;

	/**
	 * Portion of the probability space in the background model covered by known
	 * terms. This value is initialized to 0 and updated whenever setTermProbability
	 * is called.
	 **/
	double backgroundSpaceCovered;

public:

	/**
	 * Creates a new IncompleteLanguageModel that uses the given background model
	 * to approximate term frequencies for unknown terms. If the "claimOwnership"
	 * flag is set to true, the IncompleteLanguageModel will own the given background
	 * model and delete it in the destructor.
	 **/
	IncompleteLanguageModel(AbstractLanguageModel *backgroundModel, bool claimOwnership);

	~IncompleteLanguageModel();

	/**
	 * Returns the probability of the given term according to the language model.
	 * If the term's probability is known (i.e., has been set via setTermProbability),
	 * then it is returned directly. Otherwise, it is approximated using the probability
	 * values of known terms and the background language model.
	 **/
	virtual double getTermProbability(int termID);

	/** Just like getTermProbability, only for term-document probabilities instead. **/
	virtual double getDocumentProbability(int termID);

	virtual double getTermProbability(const char *term);

	virtual double getDocumentProbability(const char *term);

	/**
	 * Sets the TF probability value for the given term. If the term already has a
	 * probability value associated with it, then it is reset to the new value.
	 **/
	void setTermProbability(int termID, double p);

	/**
	 * Returns the Kullback-Leibler divergence between the two given incomplete
	 * language models. This can be done really fast, because only the known terms
	 * in the two models actually have to be compared, while the unknown terms can
	 * be ignored (more or less).
	 **/
	static double getKLD(IncompleteLanguageModel *p, IncompleteLanguageModel *q);

}; // end of class IncompleteLanguageModel


#endif


