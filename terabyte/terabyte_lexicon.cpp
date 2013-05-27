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
 * Implementation of the TerabyteLexicon class. See terabyte_lexicon.h for
 * documentation.
 *
 * author: Stefan Buettcher
 * created: 2005-05-25
 * changed: 2007-07-13
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include "terabyte_lexicon.h"
#include "terabyte_surrogates.h"
#include "../feedback/language_model.h"
#include "../filters/inputstream.h"
#include "../index/compressed_lexicon.h"
#include "../index/compressed_lexicon_iterator.h"
#include "../index/index.h"
#include "../index/index_iterator.h"
#include "../index/index_merger.h"
#include "../index/segmentedpostinglist.h"
#include "../indexcache/docidcache.h"
#include "../indexcache/indexcache.h"
#include "../misc/all.h"
#include "../stemming/stemmer.h"


using namespace std;


static const char *LOG_ID = "TerabyteLexicon";

static const char *LEXICON_FILE = "lexicon";

static const int BOOSTING_CNT = 20;

#define PROXIMITY_PRUNING 0


/**
 * We use document structure in order to increase the impact of a term that
 * is found in the document title, for example. The boosting definition pair
 * ("<title>", 4) means that every term found in the document title is counted
 * as 4 occurrences of the same term.
 **/
DocumentStructureTermBoost boostingDefinitions[BOOSTING_CNT] = {
	{ "<title>", 4 }, { "</title>", -4 },
	{ "<h1>", 3 }, { "</h1>", -3 },
	{ "<h2>", 3 }, { "</h2>", -3 },
	{ "<h3>", 3 }, { "</h3>", -3 },
	{ "<b>", 3 }, { "</b>", -3 },
	{ "<strong>", 3 }, { "</strong>", -3 },
	{ "<i>", 2 }, { "</i>", -2 },
	{ "<em>", 2 }, { "</em>", -2 },
	{ "<u>", 2 }, { "</u>", -2 },
	{ "<dochdr>", 2}, { "</dochdr>", -2 }
};


TerabyteLexicon::TerabyteLexicon(Index *owner, int documentLevelIndexing) {
	this->owner = owner;
	assert(documentLevelIndexing >= 2);
	this->documentLevelIndexing = documentLevelIndexing;
	containers = typed_malloc(byte*, MAX_CONTAINER_COUNT);

	// initialize data
	termCount = 0;
	termSlotsAllocated = INITIAL_SLOT_COUNT;
	terms = typed_malloc(CompressedLexiconEntry, termSlotsAllocated);
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = -1;
	containers[0] = (byte*)malloc(CONTAINER_SIZE);
	posInCurrentContainer = 0;
	containerCount = 1;

	// update "occupied memory" information
	memoryOccupied = termSlotsAllocated * sizeof(CompressedLexiconEntry);
	memoryOccupied += HASHTABLE_SIZE * sizeof(int32_t);
	memoryOccupied += MAX_CONTAINER_COUNT * sizeof(byte*);
	memoryOccupied += CONTAINER_SIZE;

	currentDocumentStart = -1;
	usedForDocLevel = 0;
	allocatedForDocLevel = INITIAL_DOC_LEVEL_ARRAY_SIZE;
	termsInCurrentDocument = typed_malloc(int32_t, allocatedForDocLevel);
	inputStream = NULL;
	documentStartsSeen = documentEndsSeen = 0;

	getConfigurationBool(
			"POSITIONLESS_INDEXING", &positionlessIndexing, false);
	getConfigurationDouble(
			"TERABYTE_INTRADOC_PRUNING_LAMBDA", &intraDocumentPruningLambda, 1.01);
	getConfigurationInt(
			"TERABYTE_INTRADOC_PRUNING_K", &intraDocumentPruningK, 1);
	getConfigurationBool(
			"TERABYTE_SURROGATES", &buildSurrogates, false);

	// initialize boosting table
	for (int i = 0; i < BOOST_HASHTABLE_SIZE; i++) {
		boostValue[i] = 0;
		boostTagHashValue[i] = 0;
	}
	for (int i = 0; i < BOOSTING_CNT; i++) {
		unsigned int hv = getHashValue(boostingDefinitions[i].tag);
		boostValue[hv % BOOST_HASHTABLE_SIZE] = boostingDefinitions[i].multiplier;
		boostTagHashValue[hv % BOOST_HASHTABLE_SIZE] = hv;
	}
	currentBoost = 1;
	effectiveCurrentBoost = 1;
	currentBoostStart = MAX_OFFSET;
	surrogates = NULL;
} // end of TerabyteLexicon(Index*, char*, int)


TerabyteLexicon::~TerabyteLexicon() {
	if (surrogates != NULL) {
		delete surrogates;
		surrogates = NULL;
	}
} // end of TerabyteLexicon()


typedef struct {
	int termID;
	int postingsInCurrentDocument;
	double score;
} KLDScore;

static int compareKldScore(const void *a, const void *b) {
	KLDScore *x = (KLDScore*)a;
	KLDScore *y = (KLDScore*)b;
	if (x->score > y->score)
		return -1;
	else if (x->score < y->score)
		return +1;
	else
		return 0;
}

#if PROXIMITY_PRUNING
static int32_t tokensInDocument[1024 * 1024];
int tokenCount = 0;
#endif


void TerabyteLexicon::addDocumentLevelPostings() {
	assert(this->currentDocumentStart >= 0);
	if (this->currentDocumentStart < 0)
		return;
	int stemmingLevel = owner->STEMMING_LEVEL;
	offset posting;
	offset currentDocumentStart = this->currentDocumentStart;
	if ((currentDocumentStart & DOC_LEVEL_MAX_TF) != 0)
		currentDocumentStart = (currentDocumentStart | DOC_LEVEL_MAX_TF) + 1;

	// remove everything that is not allowed at the current stemming level
	int outPos = 0;
	for (int i = 0; i < usedForDocLevel; i++) {
		int id = termsInCurrentDocument[i];
		if ((stemmingLevel < 3) || (terms[id].stemmedForm < 0) || (terms[id].stemmedForm == id))
			termsInCurrentDocument[outPos++] = id;
		else
			terms[id].postingsInCurrentDocument = 0;
	}
	usedForDocLevel = outPos;

	// apply intra-document on-the-fly index pruning if desired
	if ((usedForDocLevel > 0) && ((intraDocumentPruningLambda < 1.0) || (buildSurrogates))) {
		// obtain language model used to perform pruning from index cache; if it is not in
		// the cache yet, obtain it from the file defined by the configuration variable
		// STATIC_LANGUAGE_MODEL
		LanguageModel *languageModel = owner->getStaticLanguageModel();
		assert(languageModel->getCorpusSize() > 1);

		// For each term in the current document, compute its pruning score
		// (based on its Kullback-Leibler divergence from the corpus language model)
		// according to the rule:
		//
		//   score_T = (tf_T)^(1 - delta) * log(p / q)
		//
		// where p is the term's within-document probability (tf_T / docLen) and
		// q is the term's within-collection probablity (n_T / corpusSize).
		//
		// The delta in the exponent is a tuning parameter. We have made good
		// experience with delta around 1/10.

		// precompute the within-document score of terms with TF = 1 (speedup)
		double logDocumentProbForTF1 = log(1.0 / currentDocumentLength);

		KLDScore *kldArray = typed_malloc(KLDScore, usedForDocLevel);
#if 1
		for (int i = 0; i < usedForDocLevel; i++) {
			int termID = termsInCurrentDocument[i];
			int tf = terms[termID].postingsInCurrentDocument;

			// use the "extra" field of each term in the current document to store
			// its corpus weight, i.e., log(corpusSize / termFrequency) so that we
			// can re-use this information when computing KLD scores for the following
			// documents
			if (terms[termID].extra == 0) {
				offset tf, df;
				languageModel->getTermInfo(terms[termID].term, &tf, &df);
				if ((df <= 1) || (tf <= 2) || (terms[termID].term[0] == '<'))
					terms[termID].extra = 1;
				else {
					int corpusScore = LROUND(log(languageModel->corpusSize * 1.0 / tf) * 1000);
					if (corpusScore <= 0)
						terms[termID].extra = 1;
					else
						terms[termID].extra = corpusScore;
				}
			}

			// compute the term's KLD score, using the pre-computed "extra" value,
			// representing its log-frequency in the entire collection
			double logCorpusProb = terms[termID].extra / 1000.0;
			if (tf == 1)
				kldArray[i].score = logDocumentProbForTF1 + logCorpusProb;
			else {
				static const double delta = 0.0;
				double logDocumentProb = log(tf / (1.0 * currentDocumentLength));
				kldArray[i].score = pow(tf, 1 - delta) * (logDocumentProb + logCorpusProb);
			}
			kldArray[i].termID = termID;
			kldArray[i].postingsInCurrentDocument = tf;
		}
#endif
#if 0
		double avgdl = languageModel->corpusSize / languageModel->documentCount;
		static const double okapi_k1 = 1.2;
		static const double okapi_b = 0.5;
		for (int i = 0; i < usedForDocLevel; i++) {
			int termID = termsInCurrentDocument[i];
			int tf = terms[termID].postingsInCurrentDocument;
			offset T, df;
			languageModel->getTermInfo(terms[termID].term, &T, &df);
			kldArray[i].termID = termID;
			kldArray[i].postingsInCurrentDocument = tf;
			if ((df <= 1) || (tf <= 2) || (terms[termID].term[0] == '<'))
				kldArray[i].score = 0;
			else {
				double w = log(languageModel->documentCount * 1.0 / df);
				double K = okapi_k1 * (1 - okapi_b + okapi_b * currentDocumentLength / avgdl);
				kldArray[i].score = w * (okapi_k1 + 1) * tf / (tf + K);
			}
		}				
#endif

#if PROXIMITY_PRUNING
		// adjust pruning scores based on proximity between high-weight terms
		map<int,int> invertedTermIDs;
		double *newScores = typed_malloc(double, usedForDocLevel);
		double maxTermScore = 0;
		for (int i = 0; i < usedForDocLevel; i++) {
			invertedTermIDs[kldArray[i].termID] = i;
			newScores[i] = 0;
			if (kldArray[i].score > maxTermScore)
				maxTermScore = kldArray[i].score;
		}
		for (int i = 0; i < tokenCount; i++)
			tokensInDocument[i] = invertedTermIDs[tokensInDocument[i]];
		for (int i = 0; i < tokenCount; i++) {
			double bestNeighbor = 0;
			for (int k = 1; k < 10; k++) {
				if (i - k >= 0) {
					int who = tokensInDocument[i - k];
					if (kldArray[who].score / (k + 1.0) > bestNeighbor)
						bestNeighbor = kldArray[who].score / (k + 1.0);
				}
				if (i + k < tokenCount) {
					int who = tokensInDocument[i + k];
					if (kldArray[who].score / (k + 1.0) > bestNeighbor)
						bestNeighbor = kldArray[who].score / (k + 1.0);
				}
			}
			int who = tokensInDocument[i];
			newScores[who] += bestNeighbor * 1.0 / kldArray[who].postingsInCurrentDocument;
		}
		for (int i = 0; i < usedForDocLevel; i++) {
			if (kldArray[i].score > 0)
				kldArray[i].score = kldArray[i].score * (1 + newScores[i] / maxTermScore);
		}
		free(newScores);
		tokenCount = 0;
#endif

		// sort all terms in the current document by their pruning score
		qsort(kldArray, usedForDocLevel, sizeof(KLDScore), compareKldScore);

		if (buildSurrogates) {
			// build document surrogate for the top terms found, where "top" again
			// is defined by their KLD score
			TerabyteSurrogate surrogate;
			surrogate.termCount = 0;
			int maxSurrogateTermCount = MIN(usedForDocLevel, 12);
			for (int i = 0; i < maxSurrogateTermCount; i++) {
				if (kldArray[i].score <= 0)
					break;
				surrogate.terms[surrogate.termCount].termID =
					languageModel->getTermID(terms[kldArray[i].termID].term);
				surrogate.terms[surrogate.termCount].frequency =
					kldArray[i].postingsInCurrentDocument;
				surrogate.termCount++;
			}
			if (surrogates == NULL) {
				char *fileName = evaluateRelativePathName(owner->directory, "index.surrogates");
				surrogates = new TerabyteSurrogates(fileName, true, 40, false);
				free(fileName);
			}

			int documentID = surrogates->addSurrogate(&surrogate);
			assert(documentID == currentDocumentStart / (1 << DOC_LEVEL_SHIFT));
		}

		if (intraDocumentPruningLambda < 1.0) {
			// calculate how many terms to keep, based on lambda and k
			int toKeep = (int)(usedForDocLevel * intraDocumentPruningLambda + 1.0);
			if (toKeep < intraDocumentPruningK)
				toKeep = intraDocumentPruningK;
			if (toKeep > usedForDocLevel)
				toKeep = usedForDocLevel;

			// reset the "postingsInCurrentDocument" field for every term in the list
			for (int i = 0; i < usedForDocLevel; i++)
				terms[termsInCurrentDocument[i]].postingsInCurrentDocument = 0;

			// copy the resorted term list (sorted in order of decreasing pruning score)
			// back into the original "termsInCurrentDocument" array
			for (int i = 0; i < toKeep; i++) {
				int termID = kldArray[i].termID;
				termsInCurrentDocument[i] = termID;
				terms[termID].postingsInCurrentDocument = kldArray[i].postingsInCurrentDocument;
			}
			usedForDocLevel = toKeep;
		}

		free(kldArray);
	} // end if [intra-document pruning and surrogate creation]

	// add postings for all terms (or top terms, in case of pruning) to the index
	for (int i = 0; i < usedForDocLevel; i++) {
		int id = termsInCurrentDocument[i];
		posting = currentDocumentStart + encodeDocLevelTF(terms[id].postingsInCurrentDocument);
		addPostingForTermID(id, posting);
	}
} // end of addDocumentLevelPostings()


void TerabyteLexicon::addPostingForTermID(int termID, offset posting) {
	offset value = posting - terms[termID].lastPosting;
	assert(value > 0);
	int posInChunk = terms[termID].posInCurrentChunk;
	int sizeOfChunk = terms[termID].sizeOfCurrentChunk;
	int currentChunk = terms[termID].currentChunk;
	byte *chunkData =
			&containers[currentChunk >> CONTAINER_SHIFT][currentChunk & (CONTAINER_SIZE - 1)];
	if (posInChunk <= sizeOfChunk - 8) {
		// if we have enough free space, add without any further checks
		while (value >= 128) {
			chunkData[posInChunk++] = 128 + (value & 127);
			value >>= 7;
		}
		chunkData[posInChunk++] = value;
	}
	else {
		while (true) {
			if (posInChunk >= sizeOfChunk) {
				// create new chunk, taking into account the total memory consumption of the
				// term so far
				int newChunkSize = terms[termID].memoryConsumed >> 2;
				if (newChunkSize < INITIAL_CHUNK_SIZE)
					newChunkSize = INITIAL_CHUNK_SIZE;
				// make sure the total chunk size (including header) is a multiple of 4 so
				// that we don't get SIGBUS when accessing the header as an int32_t
				newChunkSize |= 3;
				if (newChunkSize > 247)
					newChunkSize = 247;
				int newChunk = allocateNewChunk(newChunkSize + 5);
				*((int32_t*)chunkData) = newChunk;
				chunkData = &containers[newChunk >> CONTAINER_SHIFT][newChunk & (CONTAINER_SIZE - 1)];
				terms[termID].currentChunk = currentChunk = newChunk;
				terms[termID].sizeOfCurrentChunk = sizeOfChunk = (newChunkSize + 5);
				if (terms[termID].memoryConsumed < 60000)
					terms[termID].memoryConsumed += newChunkSize;
				posInChunk = 5;
			}
			if (value < 128) {
				chunkData[posInChunk++] = value;
				break;
			}
			else {
				chunkData[posInChunk++] = 128 + (value & 127);
				value >>= 7;
			}
		}
	} // end else [less than 56 bits free in current chunk]
	terms[termID].posInCurrentChunk = posInChunk;
	terms[termID].lastPosting = posting;
	terms[termID].numberOfPostings++;
} // end of addPostingForTermID(int, offset)


int32_t TerabyteLexicon::addPosting(char *term, offset posting, unsigned int hashValue) {
	unsigned int hashSlot = hashValue % HASHTABLE_SIZE;
	int termID = hashtable[hashSlot];
	int previous = termID;
	int stemmingLevel = owner->STEMMING_LEVEL;

	if (USE_DOCUMENT_STRUCTURE) {
		if (boostTagHashValue[hashValue & (BOOST_HASHTABLE_SIZE - 1)] == hashValue) {
			int boostVal = boostValue[hashValue & (BOOST_HASHTABLE_SIZE - 1)];
			if (boostVal >= effectiveCurrentBoost) {
				currentBoost = boostVal;
				effectiveCurrentBoost = boostVal;
				currentBoostStart = posting;
				if (strcasecmp(term, "<dochdr>") == 0)
					currentBoostStart += 10;
			}
			else if (boostVal < 0) {
				currentBoost = 1;
				effectiveCurrentBoost = 1;
				currentBoostStart = MAX_OFFSET;
			}
		}
		offset delta = posting - (currentBoostStart + BOOST_LENGTH);
		if (delta > 0) {
			effectiveCurrentBoost = currentBoost - delta;
			if (effectiveCurrentBoost <= 1) {
				currentBoost = 1;
				effectiveCurrentBoost = 1;
				currentBoostStart = MAX_OFFSET;
			}
		}
	}

	// find term descriptor in hashtable
	while (termID >= 0) {
		if (terms[termID].hashValue == hashValue)
			if (strcmp(term, terms[termID].term) == 0)
				break;
		previous = termID;
		termID = terms[termID].nextTerm;
	}

	// if the term cannot be found in the lexicon, add a new entry
	if (termID < 0) {
		// termID < 0 means the term does not exist so far: create a new entry
		if (termCount >= termSlotsAllocated)
			extendTermsArray();

		// add new term slot as head of hash list
		termID = termCount++;
		strcpy(terms[termID].term, term);
		terms[termID].hashValue = hashValue;
		terms[termID].nextTerm = hashtable[hashSlot];
		hashtable[hashSlot] = termID;
		terms[termID].numberOfPostings = 0;
		terms[termID].lastPosting = 0;

		// allocate space for the first chunk; make sure the total size of the chunk
		// (including header) is a multiple of 4 so that we can access the header
		// word-aligned (avoiding SIGBUS on SUNs)
		int newChunkSize = (INITIAL_CHUNK_SIZE | 3);
		int chunk = allocateNewChunk(newChunkSize + 5);
		terms[termID].firstChunk = chunk;
		terms[termID].currentChunk = chunk;
		terms[termID].memoryConsumed = newChunkSize;
		terms[termID].sizeOfCurrentChunk = newChunkSize + 5;
		terms[termID].posInCurrentChunk = 5;
		terms[termID].postingsInCurrentDocument = 0;
		terms[termID].extra = 0;
		if ((hashValue == startDocHashValue) || (hashValue == endDocHashValue))
			if ((strcmp(term, START_OF_DOCUMENT_TAG) == 0) || (strcmp(term, END_OF_DOCUMENT_TAG) == 0))
				terms[termID].postingsInCurrentDocument = 65535;
		if ((hashValue == startDocnoHashValue) || (hashValue == endDocnoHashValue))
			if ((strcmp(term, START_OF_DOCNO_TAG) == 0) || (strcmp(term, END_OF_DOCNO_TAG) == 0))
				terms[termID].postingsInCurrentDocument = 65535;

		// set "stemmedForm" according to the situation; apply stemming if STEMMING_LEVEL > 0
		int len = strlen(term);
		if (term[len - 1] == '$')
			terms[termID].stemmedForm = -1;
		else if (stemmingLevel > 0) {
			char stem[MAX_TOKEN_LENGTH * 2];
			Stemmer::stemWord(term, stem, LANGUAGE_ENGLISH, false);
			if (stem[0] == 0)
				terms[termID].stemmedForm = termID;
			else if ((stemmingLevel < 2) && (strcmp(stem, term) == 0))
				terms[termID].stemmedForm = termID;
			else {
				len = strlen(stem);
				if (len >= MAX_TOKEN_LENGTH) {
					stem[MAX_TOKEN_LENGTH - 1] = '$';
					stem[MAX_TOKEN_LENGTH] = 0;
				}
				else {
					stem[len] = '$';
					stem[len + 1] = 0;
				}
				int32_t stemmed = addPosting(stem, posting, getHashValue(stem));
				terms[termID].stemmedForm = stemmed;
			}
		}
		else
			terms[termID].stemmedForm = termID;
	} // end if (termID < 0)
	else {
		// move term to front of list in hashtable
		if (previous != termID) {
			terms[previous].nextTerm = terms[termID].nextTerm;
			terms[termID].nextTerm = hashtable[hashSlot];
			hashtable[hashSlot] = termID;
		}

		// add posting for stemmed form
		int stemmedForm = terms[termID].stemmedForm;
		if ((stemmedForm >= 0) && (stemmedForm != termID))
			if (terms[stemmedForm].postingsInCurrentDocument < 512) {
				if (terms[stemmedForm].postingsInCurrentDocument == 0) {
					if (allocatedForDocLevel <= usedForDocLevel) {
						allocatedForDocLevel *= 2;
						typed_realloc(int32_t, termsInCurrentDocument, allocatedForDocLevel);
					}
					termsInCurrentDocument[usedForDocLevel++] = stemmedForm;
				}
				terms[stemmedForm].postingsInCurrentDocument += effectiveCurrentBoost;
			}
	} // end else [termID >= 0]

#if PROXIMITY_PRUNING
	if (terms[termID].stemmedForm >= 0)
		if (tokenCount < 1000000)
			tokensInDocument[tokenCount++] = terms[termID].stemmedForm;
#endif

	// give special treatment to "<doc>" and "</doc>" tags
	if (terms[termID].postingsInCurrentDocument > 32768) {
		if (positionlessIndexing) {
			addPostingForTermID(termID, posting);
			if (hashValue == startDocHashValue) {
				clearDocumentLevelPostings();
				currentDocumentStart = posting;
				if (documentStartsSeen <= documentEndsSeen)
					documentStartsSeen++;
			}
			else if (hashValue == endDocHashValue) {
				if (documentEndsSeen < documentStartsSeen) {
					currentDocumentLength = (posting - currentDocumentStart + 1);
					currentDocumentStart = documentEndsSeen * (DOC_LEVEL_MAX_TF + 1);
					addDocumentLevelPostings();
					documentEndsSeen++;
				}
				clearDocumentLevelPostings();
			}
		}
		else {
			addPostingForTermID(termID, posting);
			if (hashValue == startDocHashValue) {
				clearDocumentLevelPostings();
				currentDocumentStart = posting;
			}
			else if (hashValue == endDocHashValue) {
				offset cds = currentDocumentStart;
				if ((cds & DOC_LEVEL_MAX_TF) != 0)
					cds = (cds | DOC_LEVEL_MAX_TF) + 1;
				if (posting > cds + DOC_LEVEL_MAX_TF/2) {
					currentDocumentLength = (posting - currentDocumentStart + 1);
					addDocumentLevelPostings();
				}
				clearDocumentLevelPostings();
			}
		}
	} // end if (terms[termID].postingsInCurrentDocument > 32768)
	else {
		if (terms[termID].postingsInCurrentDocument == 0) {
			if (usedForDocLevel >= allocatedForDocLevel) {
				allocatedForDocLevel *= 2;
				typed_realloc(int32_t, termsInCurrentDocument, allocatedForDocLevel);
			}
			termsInCurrentDocument[usedForDocLevel++] = termID;
		}
		if (terms[termID].postingsInCurrentDocument < 512) {
			terms[termID].postingsInCurrentDocument += effectiveCurrentBoost;
		}
	} // end else

	return termID;
} // end of addPosting(char*, offset, unsigned int)


void TerabyteLexicon::addPostings(char **terms, offset *postings, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	for (int i = 0; i < count; i++)
		TerabyteLexicon::addPosting(terms[i], postings[i], getHashValue(terms[i]));
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(char**, offset*, int)


void TerabyteLexicon::addPostings(char *term, offset *postings, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	unsigned int hashValue = getHashValue(term);
	for (int i = 0; i < count; i++)
		TerabyteLexicon::addPosting(term, postings[i], hashValue);
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(char*, offset*, int)


void TerabyteLexicon::addPostings(InputToken *terms, int count) {
	bool mustReleaseWriteLock = getWriteLock();
	for (int i = 0; i < count; i++)
		TerabyteLexicon::addPosting(
				(char*)terms[i].token, terms[i].posting, terms[i].hashValue);
	if (mustReleaseWriteLock)
		releaseWriteLock();
} // end of addPostings(InputToken*, int)


void TerabyteLexicon::createCompactIndex(const char *fileName) {
	assert(termCount > 0);
	LocalLock lock(this);

	clearDocumentLevelPostings();
	char dummy[MAX_TOKEN_LENGTH * 2];
	strcpy(dummy, "<!>");

	for (int i = 0; i < termCount; i++)
		if (terms[i].postingsInCurrentDocument < 16384) {
			int len = strlen(terms[i].term);
			if (len <= MAX_TOKEN_LENGTH - 3) {
				if (strncmp(terms[i].term, "<!>", 3) != 0) {
					strcpy(&dummy[3], terms[i].term);
					strcpy(terms[i].term, dummy);
				}
			}
			else
				terms[i].numberOfPostings = 0;
		}

	int stemmingLevel = owner->STEMMING_LEVEL;
	int32_t *sortedTerms = sortTerms();
	CompactIndex *target = CompactIndex::getIndex(owner, fileName, true);

	for (int i = 0; i < termCount; i++) {
		int termID = sortedTerms[i];

		// if requested, discard all unstemmed-but-stemmable term information
		if (stemmingLevel >= 3)
			if ((terms[termID].stemmedForm >= 0) && (terms[termID].stemmedForm != termID))
				continue;
		if ((terms[termID].numberOfPostings < DOCUMENT_COUNT_THRESHOLD) ||
		    (terms[termID].numberOfPostings <= 0))
			continue;

		addPostingsToCompactIndex(target, terms[termID].term, termID);
	} // end for (int i = 0; i < termCount; i++)

	free(sortedTerms);
	delete target;
} // end of createCompactIndex(char*)


void TerabyteLexicon::mergeWithExisting(IndexIterator **iterators, int iteratorCount, char *outputIndex) {
	if (iterators == NULL) {
		createCompactIndex(outputIndex);
		return;
	}

	assert("Not implemented" == NULL);

	bool mustReleaseReadLock = getReadLock();

	clearDocumentLevelPostings();

	char dummy[MAX_TOKEN_LENGTH * 2];
	strcpy(dummy, "<!>");
	for (int i = 0; i < termCount; i++)
		if (terms[i].postingsInCurrentDocument < 16384) {
			int len = strlen(terms[i].term);
			if (len <= MAX_TOKEN_LENGTH - 3) {
				if (strncmp(terms[i].term, "<!>", 3) != 0) {
					strcpy(&dummy[3], terms[i].term);
					strcpy(terms[i].term, dummy);
				}
			}
			else
				terms[i].numberOfPostings = 0;
		}

	IndexIterator **newIterators = typed_malloc(IndexIterator*, iteratorCount + 1);
	for (int i = 0; i < iteratorCount; i++)
		newIterators[i] = iterators[i];
	newIterators[iteratorCount] = new CompressedLexiconIterator(this);
	free(iterators);
	iterators = newIterators;
	iteratorCount++;

	IndexMerger::mergeIndices(owner, outputIndex, iterators, iteratorCount);

	if (mustReleaseReadLock)
		releaseReadLock();
} // end of mergeWithExisting(...)


void TerabyteLexicon::mergeWithExisting(IndexIterator **iterators, int iteratorCount, char *outputIndex, ExtentList *visible) {
	assert("Not implemented" == NULL);
} // end of mergeWithExisting(...)


ExtentList * TerabyteLexicon::getUpdates(const char *term) {
	return new ExtentList_Empty();
} // end of getUpdates(char*)


PostingList * TerabyteLexicon::getPostingListForTerm(int termID) {
	assert("Not implemented" == NULL);
} // end of getPostingListForTerm(int)


SegmentedPostingList * TerabyteLexicon::getSegmentedPostingListForTerm(int termID) {
	assert("Not implemented" == NULL);
} // endof getSegmentedPostingListForTerm(int)


void TerabyteLexicon::getClassName(char *target) {
	strcpy(target, "TerabyteLexicon");
}


void TerabyteLexicon::setInputStream(FilteredInputStream *fis) {
	inputStream = fis;
}


IndexIterator * TerabyteLexicon::getIterator() {
	assert("Not implemented" == NULL);
	return NULL;
}



