/**
 * author: Stefan Buettcher
 * created: 2006-09-04
 * changed: 2006-09-04
 **/


#ifndef __FEEDBACK__INTERPOLATION_LANGUAGE_MODEL_H
#define __FEEDBACK__INTERPOLATION_LANGUAGE_MODEL_H


#include "abstract_language_model.h"


class InterpolationLanguageModel : public AbstractLanguageModel {

private:

	/** List of underlying language models. **/
	AbstractLanguageModel **models;

	/** Weights of the models in the linear interpolation. **/
	double *weights;

	/** Number of underlying language models. **/
	int count;

public:

	/**
	 * Creates a new language model that is an interpolation of "count" language
	 * models, combined using the given weights. Does not take ownership of the
	 * given models. Caller needs to make sure the input models remain alive while
	 * the InterpolationLanguageModel is accessing them.
	 **/
	InterpolationLanguageModel(AbstractLanguageModel **models, double *weights, int count);

	~InterpolationLanguageModel();

	/** Returns the relative frequency of the given term within the collection. **/
	virtual double getTermProbability(int termID);

	/** Returns the probability that a random document contains the given term. **/
	virtual double getDocumentProbability(int termID);

	/** Returns the relative frequency of the given term within the collection. **/
	virtual double getTermProbability(const char *term);

	/** Returns the probability that a random document contains the given term. **/
	virtual double getDocumentProbability(const char *term);

}; // end of class InterpolationLanguageModel


#endif


