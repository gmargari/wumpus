/**
 * Definition of the ThresholdIterator class. A ThresholdIterator behaves
 * like a normal iterator, but only returns posting lists with length from
 * a given interval [lowerLimit, upperLimit].
 *
 * author: Stefan Buettcher
 * created: 2007-02-15
 * changed: 2007-02-15
 **/


#ifndef __INDEX__THRESHOLD_ITERATOR_H
#define __INDEX__THRESHOLD_ITERATOR_H


#include "index_iterator.h"


class ThresholdIterator : public IndexIterator {

private:

	/** This is where we get the data from. **/
	IndexIterator *iterator;

	/** Interval for which we forward lists to the caller.**/
	int lowerLimit, upperLimit;

	/** List header for next list. NULL if done. **/
	PostingListSegmentHeader *currentHeader;

	/** Same as above, but contains the actual term string. **/
	char currentTerm[MAX_TOKEN_LENGTH + 1];

public:

	/**
	 * Creates a new ThresholdIterator that obtains its posting lists from
	 * the given iterator and forwards lists between "lowerLimit" and
	 * "upperLimit" to the caller.
	 * The ThresholdIterator will claim ownership of the given iterator. It
	 * will automatically delete it in the destructor.
	 **/
	ThresholdIterator(IndexIterator *iterator, int lowerLimit, int upperLimit);

	virtual ~ThresholdIterator();

	/**
	 * This method returns incorrect term count, taken from the underlying
	 * iterator.
	 **/
	virtual int64_t getTermCount();

	/**
	 * This method returns incorrect list count, taken from the underlying
	 * iterator.
	 **/
	virtual int64_t getListCount();

	/** Returns true iff there are more data to be returned. **/
	virtual bool hasNext();

	/** Returns a pointer to the next term. Do not touch! Do not free! **/
	virtual char *getNextTerm();

	/** Returns header for next list. Do not touch! Do not free! **/
	virtual PostingListSegmentHeader *getNextListHeader();

	virtual byte *getNextListCompressed(int *length, int *size, byte *buffer);

	virtual offset *getNextListUncompressed(int *length, offset *buffer);

	virtual void skipNext();

	virtual char *getClassName();

private:

	/** Finds next term whose list is within the user-defined interval. **/
	void jumpToNext();
	
}; // end of class ThresholdIterator


#endif


