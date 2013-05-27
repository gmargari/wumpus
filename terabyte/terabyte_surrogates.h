/**
 * This file contains the class declaration of the TerabyteSurrogates class.
 * TerabyteSurrogates is used to keep small document representations
 * (surrogates) in memory so that they can be used for reranking purposes.
 *
 * author: Stefan Buettcher
 * created: 2006-08-16
 * changed: 2006-08-26
 **/


#ifndef __TERABYTE__TERABYTE_SURROGATES_H
#define __TERABYTE__TERABYTE_SURROGATES_H


#include "../index/index_types.h"
#include "../misc/lockable.h"


/** Maximum number of terms in a document surrogate. **/
static const int MAX_SURROGATE_TERM_COUNT = 32;


class LanguageModel;


struct SurrogateTermDescriptor {
	int32_t termID;
	int32_t frequency;
}; // end of struct SurrogateTermDescriptor


struct TerabyteSurrogate {

	/** Number of terms in this surrogate. **/
	int termCount;

	SurrogateTermDescriptor terms[MAX_SURROGATE_TERM_COUNT];

}; // end of struct TerabyteSurrogate


class TerabyteSurrogates : public Lockable {

public:

	/** Number of documents for which we have surrogates. **/
	int32_t documentCount;

	/**
	 * Size of each encoded surrogate in bytes. Smaller surrogates are padded to
	 * the right length. Larger surrogates are truncated.
	 **/
	int32_t surrogateSize;

	/** Encoded surrogates. **/
	byte *surrogateData;

	/** Write buffer for encoded document surrogates. **/
	byte surrogateBuffer[2048];

	/** Fill-level of the write buffer. **/
	int surrogateBufferPos;

	/** Handle to data file. **/
	int fileHandle;

	/**
	 * If this is true, then we cannot add further surrogates. If it is false,
	 * then we cannot query the object.
	 **/
	bool readOnly;

private:

	/** Size of the file header, in bytes. **/
	static const int HEADER_SIZE = 2 * sizeof(int32_t);

public:

	/**
	 * Creates a new TerabyteSurrogates instance. If "create" is true, the instance
	 * is empty, and data will be written to the given file (the file is created
	 * and truncated to zero). Otherwise, surrogate data will be read from the
	 * given file. "loadIntoMemory" specifies whether the surrogate data are kept
	 * on disk or loaded into memory for decreased latency.
	 **/
	TerabyteSurrogates(const char *fileName, bool create,
			               int surrogateSize, bool loadIntoMemory);

	~TerabyteSurrogates();

	/**
	 * Adds the given surrogate to the document surrogate database. Returns the
	 * document ID that this surrogate has been assigned. If unsuccessful, -1
	 * is returned.
	 **/
	int addSurrogate(const TerabyteSurrogate *surrogate);

	/**
	 * Puts the surrogate for the given document ID into the "result" buffer.
	 * Returns false if no surrogate for the given document ID can be found.
	 **/
	bool getSurrogate(int documentID, TerabyteSurrogate *result);

	/**
	 * Returns the cosine similarity of the two document surrogates given
	 * by "x" and "y". The cosine similarity is defined as the normalized
	 * inner product of the term vectors, after transforming them from
	 * term frequencies to KLD scores (using the docLen* arguments and the
	 * given language model).
	 **/
	static double getCosineSimilarity(const TerabyteSurrogate *x, double docLenX,
	                                  const TerabyteSurrogate *y, double docLenY,
	                                  LanguageModel *languageModel);

	/**
	 * Returns the Kullback-Leibler divergence of the two given document
	 * surrogates, assuming that all terms that do not appear in a given surrogate
	 * are distributed according to their global frequency, as defined by the
	 * given language model.
	 **/
	static double getKLD(const TerabyteSurrogate *p, double docLenP,
	                     const TerabyteSurrogate *q, double docLenQ,
	                     LanguageModel *languageModel);

private:

	/**
	 * Creates a compact version of the given document surrogate, consuming
	 * at most "surrogateSize" bytes, and stores it in the given buffer.
	 **/
	void encodeSurrogate(const TerabyteSurrogate *surrogate, byte *buffer);

	/**
	 * Decodes a compact version of a document surrogate, created by
	 * encodeSurrogate and stored in "buffer", and puts it into "surrogate".
	 **/
	void decodeSurrogate(const byte *buffer, TerabyteSurrogate *surrogate);

}; // end of class TerabyteSurrogates


#endif


