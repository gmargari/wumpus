#ifndef __INDEX__INPLACE_INDEX_H
#define __INDEX__INPLACE_INDEX_H


#include <map>
#include <string>
#include "index_types.h"
#include "ondisk_index.h"
#include "../misc/all.h"


class Index;
class IndexIterator;


struct InPlaceTermDescriptor {

	/** The term itself. **/
	char term[MAX_TOKEN_LENGTH + 1];

	/**
	 * Sometimes, it happens that a term appears both in the merge part and in
	 * the in-place part of the on-disk index structures. That's horrible because
	 * we have to be very careful not to destroy the ordering of on-disk posting
	 * list segments. This bitarray helps us. For every on-disk index, it records
	 * whether the term appears in that index.
	 **/
	uint32_t appearsInIndex;

	/** An additional pointer to implementation-specific per-term information. **/
	void *extra;

}; // end of struct InPlaceTermDescriptor



class InPlaceIndex : public OnDiskIndex {

	friend class OnDiskIndexManager;

protected:

	/**
	 * Directory that contains the index data. Initialized to NULL and set in
	 * the constructor of the sub-class.
	 **/
	char *directory;

	/** Index instance to which this index belongs. **/
	Index *owner;

	/**
	 * Mapping from all terms in the in-place index to their in-memory
	 * descriptors.
	 **/
	std::map<std::string,InPlaceTermDescriptor> *termMap;

public:

	InPlaceIndex();

	~InPlaceIndex();

	/**
	 * Creates a new on-disk in-place index. The exact type of the index depends
	 * on the value of the appropriate configuration variable.
	 **/
	static InPlaceIndex *getIndex(Index *owner, const char *directory);

	/**
	 * Returns a pointer to the term descriptor for the given term. Don't mess
	 * with the pointer! Don't free the allocation!
	 * Returns NULL if no entry for the given term can be found.
	 **/
	InPlaceTermDescriptor *getDescriptor(const char *term);

	IndexIterator *getIterator(int bufferSize) {
		assert("Not implemented!" == NULL);
		return NULL;
	}

	/** Writes a list of all terms, and their appearance flags, to "index.long.list". **/
	void saveTermMap();

	/** Reads the list written to disk by saveTermMap(). **/
	void loadTermMap();

	/**
	 * Returns a sequence of zero-terminated strings, representing all terms in
	 * the in-place index. The sequence is terminated by a string of length 0.
	 * Memory has to be freed by the caller.
	 **/
	char *getTermSequence();

	/**
	 * Informs the in-place index that the current update operation is over. This
	 * makes the in-place index flush its internal buffers.
	 **/
	virtual void finishUpdate() = 0;

}; // end of class InPlaceIndex


#endif


