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
 * author: Stefan Buettcher
 * created: 2005-05-25
 * changed: 2007-04-25
 **/


#ifndef __TERABYTE__TERABYTE_LEXICON_H
#define __TERABYTE__TERABYTE_LEXICON_H


#include "terabyte.h"
#include "../index/index_types.h"
#include "../index/compactindex.h"
#include "../index/compressed_lexicon.h"
#include "../index/index_iterator.h"
#include "../index/lexicon.h"
#include "../index/postinglist.h"
#include "../index/segmentedpostinglist.h"
#include "../extentlist/extentlist.h"


class FilteredInputStream;
class LanguageModel;
class TerabyteSurrogates;


/** On-disk header for the TerabyteLexicon class. **/
typedef struct {

	int32_t termCount;

	offset smallestOffset, biggestOffset;
	
	offset usedAddressSpace, deletedAddressSpace;

	int32_t containerCount;

	int32_t posInCurrentContainer;

} TerabyteLexiconHeader;


/**
 * The DocumentStructureTermBoost structure is used to define boostings for
 * terms that appear within certain containers. For example, a boosting definition
 * ("<title>", 4) means that all terms that appear within the document title are
 * counted as 4 occurrences of the same term.
 **/
typedef struct {

	/** HTML start tag (e.g., "<title>"). **/
	const char *tag;

	/** How many times are the terms in the container counted? **/
	int multiplier;

} DocumentStructureTermBoost;



class TerabyteLexicon : public CompressedLexicon {

	friend class Index;

protected:

	/**
	 * Defines whether document structure ("<title>", "<h1>", ...) should be used
	 * to increase the impact of terms that appear in one of these components of
	 * a document.
	 **/
	static const bool USE_DOCUMENT_STRUCTURE = false;

	static const int BOOST_HASHTABLE_SIZE = 2048;

	/**
	 * Once we enter boosting mode, we do not stay there for an inifitely long time.
	 * Instead, we only give full boost to the first BOOST_LENGTH terms that we see.
	 * After that, the boost is decreased for every additional term, until we are
	 * back at "currentBoost == 1".
	 */
	static const int BOOST_LENGTH = 10;

	/**
	 * Tells us whether the term with hash value = i (mod BOOST_HASHTABLE_SIZE) is
	 * a start tag for a boosting container. A value greater than zero immediately
	 * translates into the boost value defined for the given start tag. A value
	 * smaller than zero indicates an end tag that terminates a boosting region.
	 **/
	int boostValue[BOOST_HASHTABLE_SIZE];

	unsigned int boostTagHashValue[BOOST_HASHTABLE_SIZE];

	/** Currently active multiplicator (1 if no boost). **/
	int currentBoost, effectiveCurrentBoost;

	/** At what position did the currently active boosting sequence start? **/
	offset currentBoostStart;

	/**
	 * If true, we throw away *all* positional information and only store raw
	 * document numbers.
	 **/
	bool positionlessIndexing;

	/**
	 * Defines the P in "top P% of all postings in the document will make it into
	 * the final index". A value grater than 1.0 means "no intra-document pruning".
	 **/
	double intraDocumentPruningLambda;

	/** Minimum number of postings we keep per document. **/
	int intraDocumentPruningK;

	/**
	 * Indicates whether we build document surrogates on-the-fly while we
	 * are indexing the text collection.
	 **/
	bool buildSurrogates;

	/** The guy who encodes and stores the document surrogates. **/
	TerabyteSurrogates *surrogates;

	/**
	 * This is for positionless indexing, where we store document IDs instead of
	 * actual positions. We count the number of "<doc>" and "</doc>" tags
	 * encountered and then use this information to compute pseudo-addresses
	 * for the document-level postings in the index.
	 **/
	offset documentStartsSeen, documentEndsSeen;

	/** This is the input stream that provides us with the input tokens. **/
	FilteredInputStream *inputStream;

public:

	TerabyteLexicon(Index *owner, int documentLevelIndexing);

	/** Deletes the object and frees all resources. **/
	~TerabyteLexicon();

	/**
	 * Same as addPosting(char*, offset), but for batched updates of the Lexicon.
	 * This is necessary if we want to avoid the overhead caused by acquiring the
	 * semaphore each time a new term/posting pair is added to the Lexicon.
	 **/
	void addPostings(char **terms, offset *postings, int count);

	/**
	 * Similar to addPostings(char**, ...) above. Adds a number of postings
	 * for the same term.
	 **/
	void addPostings(char *term, offset *postings, int count);

	/** Same as above, but different. **/
	void addPostings(InputToken *term, int count);

	/**
	 * Creates a new CompactIndex instance from the data found in the terms'
	 * update lists. The new CompactIndex's data will be found in the file
	 * specified by "fileName".
	 **/
	void createCompactIndex(const char *fileName);

	/**
	 * Creates a new CompactIndex instance that is the result of a merge operation
	 * between a set of existing CompactIndex instances ("iterator") and the content
	 * of the Lexicon. The resulting index is written to "outputIndex".
	 * Please note that "iterator" may in fact refer to a MultipleCompactIndexIterator
	 * instance.
	 **/
	void mergeWithExisting(IndexIterator **iterators, int iteratorCount, char *outputIndex);

	/** Same as above, but with built-in garbage collection. **/
	void mergeWithExisting(IndexIterator **iterators, int iteratorCount,
				char *outputIndex, ExtentList *visible);

	/**
	 * Returns an ExtentList instance that contains the postings stored in the
	 * update list that belongs to term "term".
	 **/
	ExtentList *getUpdates(const char *term);

	void setInputStream(FilteredInputStream *fis);

	/** Returns an iterator object for this lexicon. NOT IMPLEMENTED! **/
	virtual IndexIterator *getIterator();

protected:

	/** Puts "TerabyteLexicon" into the target buffer. **/
	void getClassName(char *target);

	/**
	 * Adds a posting to the given term's update list. Returns the term ID of the
	 * given term.
	 **/
	int32_t addPosting(char *term, offset posting, unsigned int hashValue);

	void addPostingForTermID(int termID, offset posting);

	/**
	 * Returns a PostingList instance that contains all the postings for the
	 * given term that have been stored in memory. The PostingList will not the
	 * actual data found inside the Lexicon, but a copy of those. This enables
	 * us to update the Lexicon content while a query is being processed.
	 **/
	PostingList *getPostingListForTerm(int termID);

	SegmentedPostingList *getSegmentedPostingListForTerm(int termID);

private:

	/**
	 * This method is only used if "documentLevelIndexing == true". It loops over
	 * the list of all terms that have appeared in the current document and adds
	 * the appropriate postings to the index.
	 **/
	void addDocumentLevelPostings();

}; // end of class TerabyteLexicon


#endif


