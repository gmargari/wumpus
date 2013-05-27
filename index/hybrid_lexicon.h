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
 * Definition of the HybridLexicon class. HybridLexicon represents a hybrid
 * approach to index maintenance, combining in-place update and re-merge. The
 * in-place component is realized by files: Long posting lists are kept in
 * separate files, one file per list (PostingListInFile class). The re-merge
 * component is implemented as the usual CompactIndex crap.
 *
 * The original reason for the hybrid approach was to find out whether ordinary
 * file systems (in particular: Reiser4) can be used as the underlying storage
 * layer for text retrieval systems. The response was: "Yes, but..." Some
 * problems arose. Hybrid index maintenance is the answer to all those problems.
 * :-)
 *
 * The implementation found in this file and in hybrid_lexicon.cpp corresponds
 * to the ECIR 2006 paper titled "A Hybrid Approach to Index Maintenance in
 * Dynamic Information Retrieval Systems".
 *
 * author: Stefan Buettcher
 * created: 2005-07-04
 * changed: 2007-04-13
 **/


#ifndef __INDEX__FILESYSTEM_LEXICON_H
#define __INDEX__FILESYSTEM_LEXICON_H


#include "lexicon.h"
#include "index_types.h"
#include "compactindex.h"
#include "compressed_lexicon.h"
#include "index_iterator.h"
#include "inplace_index.h"
#include "postinglist.h"
#include "segmentedpostinglist.h"
#include "../extentlist/extentlist.h"


class HybridLexicon : public CompressedLexicon {

public:

	/** Maximum number of CompactIndex instances controlled by this HybridLexicon. **/
	static const int MAX_COMPACTINDEX_COUNT = 32;

protected:

	/**
	 * Whenever somebody asks us how many memory we are consuming, we say: "Nothing!"
	 * Of course, this is a lie. But it prevents the Index from creating CompactIndex
	 * instances and allows us to implement the in-place indexing scheme inside the
	 * HybridLexicon class. The actual memory consumption of the object is stored
	 * in this variable.
	 **/
	int realMemoryConsumption;

	/** Value of MAX_UPDATE_SPACE, taken from the config file. **/
	int maxMemoryConsumption;

	/**
	 * This is the part of the index that we run re-merge on. These guys can
	 * be NULL to indicate that there is no such index.
	 **/
	CompactIndex *compactIndex[MAX_COMPACTINDEX_COUNT];

	InPlaceIndex *longListIndex;

	/** Greatest ID value of any CompactIndex instance. **/
	int maxIndexID;

	/**
	 * Indicates whether the savings obtained during the last partial flushing
	 * operation were worthwhile.
	 **/
	bool lastPartialFlushWasSuccessful;

	/**
	 * Duration of the previous full flush (merge with on-disk index). This value
	 * is used to optimize the partial flushing threshold.
	 **/
	double durationOfLastMerge;

public:

	HybridLexicon(Index *owner, int documentLevelIndexing);

	virtual ~HybridLexicon();

	/** Empties the lexicon. **/
	virtual void clear();

	/**
	 * Makes the lexicon almost empty. All terms that have more than "threshold"
	 * postings are kept. This way, we avoid repeating the expensive stemming.
	 **/
	virtual void clear(int threshold);

	/**
	 * Same as addPosting(char*, offset), but for batched updates of the Lexicon.
	 * This is necessary if we want to avoid the overhead caused by acquiring the
	 * semaphore each time a new term/posting pair is added to the Lexicon.
	 **/
	virtual void addPostings(char **terms, offset *postings, int count);

	/**
	 * Similar to addPostings(char**, ...) above. Adds a number of postings
	 * for the same term.
	 **/
	virtual void addPostings(char *term, offset *postings, int count);

	/** Same as above, but different. **/
	virtual void addPostings(InputToken *terms, int count);

	/** This method fails an assertion, as it should never be called. **/
	virtual void createCompactIndex(const char *fileName);

	/** This method fails an assertion, as it should never be called. **/
	virtual void mergeWithExisting(IndexIterator **iterators, int iteratorCount, char *outputIndex);

	/** This method fails an assertion, as it should never be called. **/
	virtual void mergeWithExisting(IndexIterator **iterators, int iteratorCount,
				char *outputIndex, ExtentList *visible);

	/**
	 * Returns an ExtentList instance that contains the postings stored in the
	 * update list that belongs to term "term".
	 **/
	virtual ExtentList *getUpdates(const char *term);

protected:

	/** Returns "HybridLexicon". **/
	virtual void getClassName(char *target);

private:

	/**
	 * Performs a partial flush in order to regain some memory. If that does not
	 * help, a complete flush is performed, freeing all memory.
	 **/
	void partialFlush();

	/** Writes all in-memory postings to disk. **/
	void flushPostingsToDisk();

	/**
	 * Writes all long lists with a minimum memory consumption of "minSize"
	 * bytes to disks. After this has been done, it recompacts the in-memory
	 * postings, removing all postings that have just been written to disk.
	 * This is what we call "partial flushing".
	 **/
	void flushLongListsToDisk(int minSize);

	/**
	 * Flushes the long list for the term with given term ID to disk. The index
	 * file for that term has to exist already.
	 **/
	void flushLongListToDisk(int termID);

	/**
	 * This method is called from within flushLongListsToDisk(int) and removes
	 * the wholes in the large containers that resulted from flushing the long
	 * lists to disk.
	 **/
	void recompactPostings();

}; // end of class HybridLexicon


#endif


