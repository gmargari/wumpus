/**
 * Copyright (C) 2007 Stefan Buettcher. All rights reserved.
 * This is free software with ABSOLUTELY NO WARRANTY.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA
 **/

/**
 * This is the definition of the Lexicon interface. Possible implementations
 * are: CompressedLexicon, UncompressedLexicon, and TwoPassLexicon.
 *
 * author: Stefan Buettcher
 * created: 2005-05-15
 * changed: 2007-03-05
 **/


#ifndef __INDEX__LEXICON_H
#define __INDEX__LEXICON_H


#include "index_types.h"
#include "compactindex.h"
#include "index_iterator.h"
#include "postinglist.h"
#include "segmentedpostinglist.h"
#include "../config/config.h"
#include "../extentlist/extentlist.h"
#include "../filters/inputstream.h"
#include "../misc/all.h"


#define START_OF_DOCUMENT_TAG "<doc>"
#define END_OF_DOCUMENT_TAG "</doc>"
#define START_OF_DOCNO_TAG "<docno>"
#define END_OF_DOCNO_TAG "</docno>"


class FilteredInputStream;


class Lexicon : public Lockable {

	friend class Index;
	friend class LexiconIterator;
	friend class OnDiskIndexManager;

protected:

	Index *owner;

	/** Number of terms in the lexicon. **/
	int32_t termCount;

	/** The amount of memory occupied by this Lexicon instance. **/
	int memoryOccupied;

	/**
	 * We use this information to keep track of the amount of index address space
	 * covered by the in-memory index. The information is propagated to the on-disk
	 * indices when the index data are transferred to disk. It is used for garbage
	 * collection purposes (on-the-fly GC).
	 **/
	offset firstPosting, lastPosting;

public:

	/** Initializes local variables. **/
	Lexicon();

	/** Empties the lexicon. **/
	virtual void clear() = 0;

	/**
	 * Makes the lexicon almost empty. All terms that have less than "threshold"
	 * postings are kept.
	 **/
	virtual void clear(int threshold) = 0;

	/** Batched update for sequences of (term,posting) pairs. **/
	virtual void addPostings(char **terms, offset *postings, int count) = 0;

	/**
	 * Similar to addPostings(char**, ...) above. Adds a number of postings
	 * for the same term.
	 **/
	virtual void addPostings(char *term, offset *postings, int count) = 0;

	/** Same as above, but different. **/
	virtual void addPostings(InputToken *terms, int count) = 0;

	/**
	 * Creates a new CompactIndex instance from the data found in the terms'
	 * update lists. The new CompactIndex's data will be found in the file
	 * specified by "fileName".
	 **/
	virtual void createCompactIndex(const char *fileName) = 0;

	/**
	 * Creates a new CompactIndex instance that is the result of a merge operation
	 * between a set of existing CompactIndex instances ("iterator") and the content
	 * of the Lexicon. The resulting index is written to "outputIndex".
	 * Please note that "iterator" may in fact refer to a MultipleCompactIndexIterator
	 * instance.
	 **/
	virtual void mergeWithExisting(IndexIterator **iterators, int iteratorCount,
			char *outputIndex) = 0;

	/** Same as above, but with built-in garbage collection. **/
	virtual void mergeWithExisting(IndexIterator **iterators, int iteratorCount,
				char *outputIndex, ExtentList *visible) = 0;

	/**
	 * Returns an ExtentList instance that contains the postings stored in the
	 * update list that belongs to term "term".
	 **/
	virtual ExtentList *getUpdates(const char *term) = 0;

	/** Stores the class name ("Lexicon") in the given buffer. **/
	virtual void getClassName(char *target);

	/** Returns the number of terms in the Lexicon. **/
	virtual int64_t getTermCount() { return termCount; }

	/** Returns an iterator object for this lexicon. **/
	virtual IndexIterator *getIterator() = 0;

	/**
	 * Notifies the Lexicon instance of the current input stream. This is necessary
	 * because we want the Lexicon to be able to communicate with the input stream
	 * directly.
	 **/
	virtual void setInputStream(FilteredInputStream *fis);

	/** Returns the hash value of the given string. **/
	static inline uint32_t getHashValue(const char *string) {
		uint32_t result = 0;
		for (int i = 0; string[i] != 0; i++)
			result = (result * 127) + ((byte)string[i]);
		return result;
	}

	/**
	 * This method is propagate index coverage information through various
	 * parts of the index organization. It is called from within Index::addFile.
	 **/
	virtual void setIndexRange(offset firstPosting, offset lastPosting);

	/**
	 * Counterpart to setIndexRange(offset, offset). Returns the value of the
	 * first and the last posting in the index.
	 **/
	virtual void getIndexRange(offset *firstPosting, offset *lastPosting);

	/**
	 * Similar to setIndexRange. This method extents the index range defined by
	 * the object's "firstPosting" and "lastPosting" member variables so that the
	 * new range includes the range given by the two arguments.
	 **/
	virtual void extendIndexRange(offset first, offset last);

}; // end of class Lexicon


static uint32_t startDocHashValue = Lexicon::getHashValue((char*)START_OF_DOCUMENT_TAG);
static uint32_t endDocHashValue = Lexicon::getHashValue((char*)END_OF_DOCUMENT_TAG);
static uint32_t startDocnoHashValue = Lexicon::getHashValue((char*)START_OF_DOCNO_TAG);
static uint32_t endDocnoHashValue = Lexicon::getHashValue((char*)END_OF_DOCNO_TAG);


#endif


