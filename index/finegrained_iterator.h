/**
 * The FineGrainedIterator class provides a new interface to the IndexIterator
 * class that can be used to obtain individual postings, or small groups of
 * postings, without the need to acquire an entire posting list segment in
 * every call.
 *
 * author: Stefan Buettcher
 * created: 2007-04-03
 * changed: 2007-04-03
 **/


#ifndef __INDEX__FINEGRAINED_ITERATOR_H
#define __INDEX__FINEGRAINED_ITERATOR_H


#include "index_types.h"


class IndexIterator;


class FineGrainedIterator {

private:

	IndexIterator *iterator;

	char currentTerm[MAX_TOKEN_LENGTH * 2];

	offset *currentSegment;

	int currentSegmentSize, currentSegmentPos;

public:

	/**
	 * Creates a new iterator that provides fine-grained access to the underlying
	 * index iterator given by "iterator". The new iterator will take control of
	 * the old one and will delete it in the destructor.
	 **/
	FineGrainedIterator(IndexIterator *iterator);

	~FineGrainedIterator();

	/** Advances the internal pointer to the next term in the input iterator. **/
	void advanceToNextTerm();

	/**
	 * Returns a pointer to the current term (the one to which the next posting
	 * belongs). Do not mess with the pointer returned! Do not free it!
	 * Returns NULL if there are no more terms.
	 **/
	char *getCurrentTerm();

	/**
	 * Returns the next posting for the current term. MAX_OFFSET if there are
	 * no more such postings.
	 **/
	offset getNextPosting();

	/**
	 * Puts the next m (0 <= m <= n) postings for the current term into the output
	 * buffer. Returns the number of postings (m) actually put into the buffer.
	 **/
	int getNextNPostings(int n, offset *buffer);

private:

	void reload();
	
}; // end of class FineGrainedIterator


#endif


