/**
 * This file defines the class LanguageModel, which is use to represent unigram
 * language models. It provides methods that can be used to look up term
 * frequencies, document frequencies, etc.
 *
 * author: Stefan Buettcher
 * created: 2006-01-27
 * changed: 2006-09-04
 **/


#ifndef __FEEDBACK__LANGUAGE_MODEL_H
#define __FEEDBACK__LANGUAGE_MODEL_H


#include "abstract_language_model.h"
#include "../index/index.h"
#include "../index/index_types.h"


typedef struct {

	/** The term itself. **/
	char term[MAX_TOKEN_LENGTH + 1];

	/** Its stemmed form. **/
	char stemmed[MAX_TOKEN_LENGTH + 1];

	/** For chaining in the hash table. **/
	int32_t next;

	/** Number of occurrences within the text collection. **/
	offset termFrequency;

	/** Number of documents containing the term. **/
	offset documentCount;

} LanguageModelTermDescriptor;


class LanguageModel : public AbstractLanguageModel {

	friend class Feedback;
	friend class TerabyteQuery;

public:

	double corpusSize, documentCount;

protected:

	static const double EPSILON = 1.0E-9;

	static const int INITIAL_TERM_SLOTS = 1024;

	static const int INITIAL_HASHTABLE_SIZE = 1024;

	static const double ARRAY_GROWTH_RATE = 1.37;

	/** Number of tokens and number of documents in the collection. **/
	/** An array of term descriptors. **/
	LanguageModelTermDescriptor *terms;

	/** This is the number of term slots allocated in the above array. **/
	int termSlotsAllocated;

	/** This is the number of term slots that are actually used (no. of terms). **/
	int termSlotsUsed;

	/** Number of elements in the hash table. **/
	int hashTableSize;

	/**
	 * An array of hashTableSize integers, realizing a hash table for efficient
	 * term descriptor lookup.
	 **/
	int32_t *hashTable;

	/** Tells us whether terms in this language model are stemmed or not. **/
	bool stemmed;

	/**
	 * Tells us whether we wan to use the built-in cache of the stemmer. Can
	 * be triggered by enableStemmingCache(). Note that the stemming cache is
	 * not thread-safe.
	 **/
	bool useStemmingCache;

public:

	/**
	 * Creates a new LanguageModel instance by loading data from an existing file,
	 * created by saveToFile(char*).
	 **/
	LanguageModel(char *fileName);

	/** Creates a new language model with the given parameters. **/
	LanguageModel(double corpusSize, double documentCount, bool stemmed);

	/**
	 * Creates a new language model from the data found between index addresses
	 * "start" and "end", obtained by issuing an @get query.
	 **/
	LanguageModel(Index *index, offset start, offset end, bool stemmed);

	/**
	 * Creates a new language model by merging the models given by parameters
	 * "models" and "modelCount". If at least one of the input models is stemmed,
	 * then the output model will be stemmed, too.
	 **/
	LanguageModel(LanguageModel **models, int modelCount);

	/** Class destructor. Boring. **/
	~LanguageModel();

	/**
	 * Saves the contents of the language model to the file specified by the
	 * parameters. The file created this way can be used in conjunction with
	 * the LanguageModel(char*) constructor to reload a language model from disk.
	 **/
	void saveToFile(char *fileName);

	/**
	 * Returns the number of distinct terms with non-zero probability in the
	 * language model.
	 **/
	virtual int getTermCount();

	/** Returns the relative frequency of the given term within the collection. **/
	virtual double getTermProbability(int termID);

	/** Same a above, but for a string argument instead of a term ID. **/
	virtual double getTermProbability(const char *term);

	/** Returns the probability that a random document contains the given term. **/
	virtual double getDocumentProbability(int termID);

	/** Same a above, but for a string argument instead of a term ID. **/
	virtual double getDocumentProbability(const char *term);

	/**
	 * Adds the given term to the language model. If the term already exists,
	 * previous data are overridden.
	 **/
	void addTerm(char *term, offset termFrequency, offset documentCount);

	/**
	 * Removes all information about the given term from the language model. If
	 * there is no information to be removed, nothing happens.
	 **/
	void removeTerm(char *term);

	/** Updates the given term, adding deltas to its TF and DF components. **/
	void updateTerm(char *term, offset deltaTF, offset deltaDF);

	/** Same as above, just in larger scale. **/
	void addLanguageModel(LanguageModel *m);

	/**
	 * Puts the term's frequency and document count into the variables referenced
	 * by the two pointers.
	 **/
	void getTermInfo(const char *term, offset *termFrequency, offset *documentCount);

	/** Same as above, but takes an explicit term ID instead of a term string. **/
	void getTermInfo(int termID, offset *termFrequency, offset *documentCount);

	/** Returns the term's unique term ID, or -1 if it cannot be found. **/
	int getTermID(char *term);

	/**
	 * Returns the corpus size, the number of tokens in the collection represented
	 * by this language model.
	 **/
	double getCorpusSize() { return corpusSize; }

	/**
	 * Returns the string representation of the term with the given term ID; NULL
	 * if there is no term with the given ID. Memory has to be freed by the caller.
	 **/
	char *getTermString(int termID);

	/** Just like the above, except that it returns the stemmed form (with '$'). **/
	char *getStemmedTermString(int termID);

	/**
	 * Restricts the language model to the "newTermCount" most frequent terms.
	 * If the limit is greater than the current number of terms in the model,
	 * nothing happens.
	 **/
	void restrictToMostFrequent(int newTermCount);

	/**
	 * Computes the Kullback-Leibler Divergence for the two language models
	 * given by "p" and "q", using their relative term frequencies as probability
	 * distributions. In this context, "q" is the background model.
	 **/
	static double kullbackLeiblerDivergence(LanguageModel *p, AbstractLanguageModel *q);

	static double kullbackLeiblerDivergence(LanguageModel *p, LanguageModel *q);

	/**
	 * Similar to the above, but "backgroundModel" is used to smooth term probabilities
	 * from "q" in order to avoid data sparseness problems.
	 **/
	static double getKLD(
			LanguageModel *p, AbstractLanguageModel *q, AbstractLanguageModel *backgroundModel);

	/** Returns the Kullback-Leibler Divergence for the two given probabilities. **/
	static double kullbackLeiblerDivergence(double p, double q, double corpusSize);

	/** Sets the document frequency values for all terms in the language model to "df". **/
	void setAllDocumentFrequencies(offset df);

	void enableStemmingCache();

private:

	void initialize();

	int addTermDescriptor();

	void removeTermDescriptor(int termID);

	char *normalizeTerm(char *term);

	void resizeHashTable(int size);

}; // end of class LanguageModel


#endif


