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
 * The CompressedLexicon class keeps track of all terms and postings lists that
 * are currently stored in memory. For in-memory inversion, it uses a big hash
 * table with chaining and move-to-front heuristics. All postings are compressed
 * on-the-fly as they enter the Lexicon.
 *
 * author: Stefan Buettcher
 * created: 2005-01-10
 * changed: 2007-07-04
 **/


#ifndef __INDEX__COMPRESSED_LEXICON_H
#define __INDEX__COMPRESSED_LEXICON_H


#include "lexicon.h"
#include "index_types.h"
#include "compactindex.h"
#include "index_iterator.h"
#include "postinglist.h"
#include "segmentedpostinglist.h"
#include "../config/config.h"
#include "../extentlist/extentlist.h"


/**
 * This structure is used to describe entries in the Lexicon, aka index terms.
 **/
struct CompressedLexiconEntry {

	/** What was the last posting? We need this to compute the Delta values. **/
	offset lastPosting;

	/** The term itself. **/
	char term[MAX_TOKEN_LENGTH + 1];

	/**
	 * Its hash value. We don't want to call strcmp all the time when we walk
	 * through the collision list, so we compare hash values instead and only
	 * call strcmp when both hash values are equal (extremely unlikely).
	 **/
	uint32_t hashValue;

	/**
	 * Hashtable collisions are resolved using a linked list. "nextTerm" is the
	 * successor in the linked list of a given hash slot. "nextTerm < 0" indicates
	 * the end of the list.
	 **/
	int32_t nextTerm;

	/** How many postings do we have in memory for this term? **/
	int32_t numberOfPostings;

	/**
	 * Since one chunk will no be enough for a given term, we can have multiple
	 * chunks for one term. "currentChunk" tells us where we can find the
	 * current chunk.
	 **/
	int32_t currentChunk;

	/**
	 * Note that we organize in-memory postings in big containers of size 1 MB
	 * each. In each of these containers, we can have many small chunks for the
	 * postings of a given term. "firstChunk" is the memory position of the
	 * first chunk for the given term.
	 **/
	int32_t firstChunk;

	/**
	 * When stemming is turned on, we store one posting for the stemmed form as
	 * well whenever we encounter an ordinary term: "university" -> "$univers".
	 * To avoid stemming the same term many times, we store the term ID of the
	 * stemmed form here. "stemmedForm < 0" means that the term is already stemmed;
	 * "stemmedForm == termID" means: not stemmable or self-stemmer
	 **/
	int32_t stemmedForm;

	/**
	 * This guy helps us speed up query processing by storing "number of occurrences
	 * inside document" for terms. Initialized to zero at term creation. We do not
	 * count beyond 16384. Values greater than 32767 mean: meta-posting (doc, or doc-level).
	 **/
	uint16_t postingsInCurrentDocument;

	/**
	 * This is the amount of memory consumed by the term's postings. We do not count
	 * beyond 60000.
	 **/
	uint16_t memoryConsumed;

	/**
	 * This is some free space that can be used to store implementation-specific
	 * information, e.g., global term frequencies, as needed by the document-centric
	 * pruning method implements in TerabyteLexicon.
	 **/
	uint16_t extra;

	/** Where are we in the current chunk? **/
	uint8_t posInCurrentChunk;

	/** How far may we go in the current chunk without creating a mess? **/
	uint8_t sizeOfCurrentChunk;

};



class CompressedLexicon : public Lexicon {

	friend class Index;
	friend class CompressedLexiconIterator;

public:

	/**
	 * Size of the hashtable that keeps track of terms. We better take a power
	 * of 2 here, because otherwise the modulo operation will wreck our indexing
	 * performance.
	 **/
	static const int HASHTABLE_SIZE = LEXICON_HASHTABLE_SIZE;

	/** Initial size of the slot array. **/
	static const int INITIAL_SLOT_COUNT = 1024;

	/**
	 * When we first create a chunk for new postings of a given term, we set its
	 * size to INITIAL_CHUNK_SIZE. After that, we will give the new chunk a
	 * size equal to the size allocated so far, up to a maximum of 256 bytes.
	 **/
	static const int INITIAL_CHUNK_SIZE = LEXICON_INITIAL_CHUNK_SIZE;

	/**
	 * Value between 1 and 32 (corresponding to 1/32..32/32). This is the
	 * $k$ parameter in the single-pass indexing paper.
	 **/
	static const int CHUNK_GROWTH_RATE = (int)(LEXICON_CHUNK_GROWTH_RATE * 32) - 32;

	/** CONTAINER_SIZE == (1 << CONTAINER_SHIFT). **/
	static const int CONTAINER_SHIFT = 19;

	/**
	 * All chunks are stored inside big containers. When a container is full,
	 * a new one will be allocated. Each container has size CONTAINER_SIZE.
	 **/
	static const int CONTAINER_SIZE = (1 << CONTAINER_SHIFT);

	/** Maximum number of containers we can have. **/
	static const int MAX_CONTAINER_COUNT = (1 << (31 - CONTAINER_SHIFT));

	/**
	 * When extending the arrays, we make sure that the new array size is
	 * "SLOT_GROWTH_RATE * termCount".
	 **/
	static const double SLOT_GROWTH_RATE = 1.21;

	static const int INITIAL_DOC_LEVEL_ARRAY_SIZE = 65536;

protected:

	/** An array containing all the terms in the lexicon. **/
	CompressedLexiconEntry *terms;

	/** Number of term slots allocated (size of the "terms" array). **/
	int32_t termSlotsAllocated;

	/**
	 * Hashtable mapping from strings to term descriptor IDs (entry points to
	 * linked lists).
	 **/
	int32_t hashtable[HASHTABLE_SIZE];

	/** Number of containers we have for the postings. **/
	int32_t containerCount;

	/** We need to know where we are when we insert new chunks into the container. **/
	int32_t posInCurrentContainer;

	/** The containers themselves. **/
	byte **containers;

	/**
	 * If this guy is > 0, we store additional index information that tells us
	 * immediately how many occurrences of a particular term there are within a
	 * given document. If "documentLevelIndexing == 2", we throw away all
	 * positional information.
	 **/
	int documentLevelIndexing;

	/** In case of document-level enabled: start offset of current document. **/
	offset currentDocumentStart, currentDocumentLength;

	/**
	 * Number of slots allocated and used for document-level indexing (this is
	 * the size of the "termsInCurrentDocument" array).
	 **/
	int allocatedForDocLevel, usedForDocLevel;

	/**
	 * List of IDs for all terms that have appeared in the current document so
	 * far. To be used by "addDocumentLevelPostings".
	 **/
	int32_t *termsInCurrentDocument;

public:

	/** Default constructor. **/
	CompressedLexicon();

	/** Creates a new Lexicon instance. **/
	CompressedLexicon(Index *owner, int documentLevelIndexing);

	/** Deletes the object and frees all resources. **/
	virtual ~CompressedLexicon();

	/** Empties the lexicon. **/
	void clear();

	/**
	 * Makes the lexicon almost empty. All terms that have less than "threshold"
	 * postings are kept.
	 **/
	void clear(int threshold);

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
	void addPostings(InputToken *terms, int count);

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

	/** Returns a CompressedLexiconIterator object for this lexicon. **/
	IndexIterator *getIterator();

	void getClassName(char *target);

protected:

	/** Adds the postings for the given term to the target CompactIndex instance. **/
	void addPostingsToCompactIndex(CompactIndex *target, char *term, int termID);

	/**
	 * Adds a posting to the given term's update list. Returns the term ID of the
	 * given term.
	 **/
	int32_t addPosting(char *term, offset posting, unsigned int hashValue);

	/**
	 * Returns a PostingList instance that contains all the postings for the
	 * given term that have been stored in memory. The PostingList will not the
	 * actual data found inside the Lexicon, but a copy of those. This enables
	 * us to update the Lexicon content while a query is being processed.
	 **/
	PostingList *getPostingListForTerm(int termID);

	SegmentedPostingList *getSegmentedPostingListForTerm(int termID);

	/** Creates new space in the "terms" array. **/
	void extendTermsArray();

	/**
	 * Allocates a new chunk and inserts it into one of the containers. Returns
	 * a reference to the chunk. The chunk can be found at:
	 * &containers[result / CONTAINER_SIZE][result % CONTAINER_SIZE]
	 **/
	int32_t allocateNewChunk(int size);

	/**
	 * Sorts the terms in ascending order. Returns an array that contains the new
	 * term ordering.
	 **/
	int32_t *sortTerms();

	/** Sorts the term IDs stored in "idArray" using SelectionSort. **/
	static void selectionSort(CompressedLexiconEntry *terms, int32_t *idArray, int count);

	/**
	 * Sorts the term IDs stored in "idArray" using MergeSort. Uses "tempArray"
	 * as a temp buffer for the merging.
	 **/
	static void mergeSort(CompressedLexiconEntry *terms, int32_t *idArray, int32_t *tempArray, int count);

	/**
	 * Sorts the term IDs stored in "idArray" using BucketSort. MergeSort is used to sort
	 * the individual buckets.
	 **/
	static void hybridBucketSort(CompressedLexiconEntry *terms, int32_t *idArray, int count);

	/**
	 * This method is only used if "documentLevelIndexing == true". It loops over
	 * the list of all terms that have appeared in the current document and adds
	 * the appropriate postings to the index.
	 **/
	void addDocumentLevelPostings();

	/**
	 * This method is only used if "documentLevelIndexing == true". It empties the
	 * list of term IDs for which document-level information is available.
	 **/
	void clearDocumentLevelPostings();

	/**
	 * Called from within clear(int), after removing all long lists from the
	 * lexicon.
	 **/
	void recompactPostings();

}; // end of class CompressedLexicon


#endif


