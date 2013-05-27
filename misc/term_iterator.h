/**
 * Definition of the TermIterator class. TermIterator can be used to iteratively
 * access a sequence of terms, one at a time. It can be used to exchange sets of
 * terms in a space-efficient manner. Terms are stored in front-coded form, so
 * compression is only achieved if they are sorted in lexicographical order.
 *
 * TermIterator is not thread-safe!
 *
 * author: Stefan Buettcher
 * created: 2007-02-11
 * changed: 2007-02-11
 **/


#ifndef __MISC__TERM_ITERATOR_H
#define __MISC__TERM_ITERATOR_H


class TermIterator {

public:

	/** Maximum byte length of a term. **/
	static const int MAX_LENGTH = 127;

private:

	/** Amount of space allocated for terms initially. **/
	static const int INITIAL_ALLOCATION = 1024;

	/** Buffer used to store terms. **/
	char *termBuffer;

	/** Pointer to previous term (for reader) and last term (for writer). **/
	char prevTerm[MAX_LENGTH], lastTerm[MAX_LENGTH];

	/** Pointers into "termBuffer". Used by reader and writer. **/
	int allocated, used, consumed;

	/** Maximum byte length of any term in the sequence. **/
	int maxTermLength;

public:

	TermIterator();

	~TermIterator();

	/** Adds a term at the end of the iterator. **/
	void addTerm(const char *term);

	/** Returns the maximum length of any term in the sequence. **/
	int getMaxTermLength();

	/**
	 * Returns a pointer to the first unseen term in the sequence defined by this
	 * TermIterator object. Memory has to be freed by the caller. If "buffer" is
	 * non-NULL, the method stored the next term in the given buffer and returns
	 * a pointer to that buffer. "buffer" needs to have room for at least
	 * getMaxTermLength() bytes.
	 * Returns NULL if there are no more unseen terms.
	 **/
	char *getNext(char *buffer);
	
}; // end of class TermIterator


#endif

