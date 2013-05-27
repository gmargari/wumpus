/**
 * AbstractLanguageModel is the base class for the different language models
 * defined. It supports basic operations for obtaining term frequencies and
 * document frequencies based on numeric term IDs.
 *
 * author: Stefan Buettcher
 * created: 2006-08-27
 * changed: 2006-09-04
 **/


#ifndef __FEEDBACK__ABSTRACT_LANGUAGE_MODEL_H
#define __FEEDBACK__ABSTRACT_LANGUAGE_MODEL_H


#include <sys/types.h>
#include "../misc/all.h"


class AbstractLanguageModel : public Lockable {

public:

	/** Returns the relative frequency of the given term within the collection. **/
	virtual double getTermProbability(int termID) = 0;

	/** Returns the probability that a random document contains the given term. **/
	virtual double getDocumentProbability(int termID) = 0;

	/** Returns the relative frequency of the given term within the collection. **/
	virtual double getTermProbability(const char *term) = 0;

	/** Returns the probability that a random document contains the given term. **/
	virtual double getDocumentProbability(const char *term) = 0;

}; // end of class AbstractLanguageModel


#endif


