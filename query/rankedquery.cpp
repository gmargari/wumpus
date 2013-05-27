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
 * RankedQuery implementation.
 *
 * author: Stefan Buettcher
 * created: 2004-09-28
 * changed: 2009-02-01
 **/


#include <ctype.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "rankedquery.h"
#include "bm25query.h"
#include "bm25f_query.h"
#include "cdrquery.h"
#include "desktopquery.h"
#include "gclquery.h"
#include "npquery.h"
#include "ponte_croft.h"
#include "languagemodel_query.h"
#include "qapquery.h"
#include "qap2query.h"
#include "querytokenizer.h"
#include "vectorspace_query.h"
#include "../feedback/dmc.h"
#include "../feedback/language_model.h"
#include "../feedback/interpolation_language_model.h"
#include "../feedback/qrels.h"
#include "../feedback/relevance_model.h"
#include "../misc/all.h"
#include "../misc/document_analyzer.h"
#include "../stemming/stemmer.h"
#include "../terabyte/terabyte_query.h"


using namespace std;


static const char *LOG_ID = "RankedQuery";

static const char *COMMANDS[20] = {
	"rank",
	"bm25",
	"bm25f",
	"bm25tera",
	"cdr",
	"lm",
	"lmd",
	"pontecroft",
	"desktop",
	"np",
	"okapi",
	"phoneme",
	"qap",
	"qap2",
	NULL
};


// declare RankedQuery class variables
const float RankedQuery::LOG_2;
const double RankedQuery::MAX_QTW;
const int RankedQuery::MAX_SCORER_COUNT;


void RankedQuery::initialize() {
	memset(elementQueries, 0, sizeof(elementQueries));
	queryID = NULL;
	runID = NULL;
	actualQuery = NULL;
	syntaxErrorDetected = false;
	containerQuery = NULL;
	statisticsQuery = NULL;
	elementCount = originalElementCount = 0;
	results = NULL;
	position = 0;
	performReranking = RERANKING_NONE;
	feedbackQrels = NULL;
} // end of initialize()


RankedQuery::RankedQuery() {
	initialize();
} // end of RankedQuery()


RankedQuery::RankedQuery(Index *index, const char *command, const char **modifiers,
			const char *body, uid_t userID, int memoryLimit) {
	initialize();
	actualQuery = NULL;
	syntaxErrorDetected = false;

	// try to find out which command this is; since we support queries like
	// "rank[bm25] ...", we have to process the modifiers here
	char *cmd = duplicateString(command);
	if (strcasecmp(cmd, "rank") == 0) {
		for (int i = 0; modifiers[i] != NULL; i++)
			if (strcasecmp(modifiers[i], "rank") != 0) {
				for (int k = 0; COMMANDS[k] != 0; k++)
					if (strcasecmp(modifiers[i], COMMANDS[k]) == 0) {
						free(cmd);
						cmd = duplicateString(modifiers[i]);
					}
			}
	} // end if (strcasecmp(cmd, "rank") == 0)

	if ((strcasecmp(cmd, "okapi") == 0) || (strcasecmp(cmd, "bm25") == 0))
		actualQuery =
			new BM25Query(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "bm25f") == 0)
		actualQuery =
			new BM25FQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "bm25tera") == 0)
		actualQuery =
			new TerabyteQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "cdr") == 0)
		actualQuery =
			new CDRQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if ((strcasecmp(cmd, "vectorspace") == 0) || (strcasecmp(cmd, "vsm") == 0))
		actualQuery =
			new VectorSpaceQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "desktop") == 0)
		actualQuery =
			new DesktopQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if ((strcasecmp(cmd, "lm") == 0) || (strcasecmp(cmd, "lmd") == 0))
		actualQuery =
			new LanguageModelQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "pontecroft") == 0)
		actualQuery =
			new PonteCroft(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "qap") == 0)
		actualQuery =
			new QAPQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "qap2") == 0)
		actualQuery =
			new QAP2Query(index, cmd, modifiers, body, userID, memoryLimit);
	else if (strcasecmp(cmd, "np") == 0)
		actualQuery =
			new NPQuery(index, cmd, modifiers, body, userID, memoryLimit);
	else
		syntaxErrorDetected = true;

	free(cmd);
} // end of RankedQuery(Index*, char*, char**, char*, uid_t, int)


RankedQuery::~RankedQuery() {
	if ((actualQuery != NULL) && (actualQuery != this))
		delete actualQuery;
	actualQuery = NULL;
	if (queryID != NULL) {
		free(queryID);
		queryID = NULL;
	}
	if (runID != NULL) {
		free(runID);
		runID = NULL;
	}
	for (int i = 0; i < elementCount; i++)
		if (elementQueries[i] != NULL) {
			delete elementQueries[i];
			elementQueries[i] = NULL;
		}
	if (results != NULL) {
		free(results);
		results = NULL;
	}
	if (containerQuery != NULL) {
		delete containerQuery;
		if (statisticsQuery == containerQuery)
			statisticsQuery = NULL;
		containerQuery = NULL;
	}
	if (statisticsQuery != NULL) {
		delete statisticsQuery;
		statisticsQuery = NULL;
	}
	if (additionalQuery != NULL) {
		delete additionalQuery;
		additionalQuery = NULL;
	}
	if (feedbackQrels != NULL) {
		free(feedbackQrels);
		feedbackQrels = NULL;
	}
} // end of ~RankedQuery()


bool RankedQuery::parse() {
	if (syntaxErrorDetected)
		return false;
	else if ((actualQuery != NULL) && (actualQuery != this)) {
		syntaxErrorDetected = !(actualQuery->parse());
		return !syntaxErrorDetected;
	}
	else {
		char defaultContainer[MAX_CONFIG_VALUE_LENGTH];
		if (!getConfigurationValue("DEFAULT_RETRIEVAL_SET", defaultContainer))
			strcpy(defaultContainer, DOC_QUERY);
		if (!parseQueryString(queryString, defaultContainer, NULL, memoryLimit)) {
			syntaxErrorDetected = finished = true;
			ok = false;
		}
		else {
			if (statisticsQuery == NULL)
				statisticsQuery = containerQuery;
			processQuery();
			ok = true;
		}
		return ok;
	}
} // end of parse()


void RankedQuery::processQuery() {
	if (verbose)
		addVerboseString("Query ID", queryID);

	int originalCount = count;
	if (performReranking != RERANKING_NONE)
		count = count + 20;
	for (int i = 0; i < elementCount; i++)
		internalWeights[i] = externalWeights[i];

	// perform feedback if requested
	if (feedbackMode != Feedback::FEEDBACK_NONE) {
		count = MAX(count, feedbackDocs);
		processCoreQuery();
		sortResultsByScore(results, count, false);
		feedback(feedbackDocs, feedbackTerms, feedbackStemming);
		if (results != NULL) {
			free(results);
			results = NULL;
		}
		count = originalCount;
	}

	// do the core query processing, potentially including query terms
	// added by the pseudo-relevance feedback step
	processCoreQuery();
	sortResultsByScore(results, count, false);

	// perform reranking step if requested
	if (performReranking == RERANKING_KLD) {
		double totalWeight = 0.0;
		for (int i = 0; i < elementCount; i++)
			totalWeight += internalWeights[i];
//		rerankResultsKLD(10, totalWeight);
		rerankResultsKLD(10, 1, relevanceModelMethod);
	}
	else if (performReranking == RERANKING_BAYES) {
		rerankResultsBayes(15);
	}
	else if (performReranking == RERANKING_LINKS) {
		rerankResultsLinks(count);
	}
	if (count > originalCount)
		count = originalCount;

//	analyzeKLD();
} // end of processQuery()


bool RankedQuery::getStatus(int *code, char *description) {
	if (actualQuery == NULL) {
		*code = STATUS_ERROR;
		strcpy(description, "Type of ranked query not specified (or illegal type).");
		return true;
	}
	if (syntaxErrorDetected) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
		return true;
	}
	*code = STATUS_OK;
	strcpy(description, "Ok.");
	return true;
} // end of getStatus(int*, char*)


void RankedQuery::printResultLine(char *line, ScoredExtent sex) {
	sprintf(line, "%s %.5f " OFFSET_FORMAT " " OFFSET_FORMAT,
			queryID, sex.score, sex.from, sex.to);
} // end of printResultLine(char*, ScoredExtent)


bool RankedQuery::getNextLine(char *line) {
	if (syntaxErrorDetected)
		return false;
	if (verboseText != NULL) {
		strcpy(line, verboseText);
		free(verboseText);
		verboseText = NULL;
		return true;
	}
	if ((actualQuery != NULL) && (actualQuery != this))
		return actualQuery->getNextLine(line);
	if (!ok) {
		finished = true;
		return false;
	}
	if (position >= count) {
		finished = true;
		return false;
	}
	if (results[position].score <= 0.0) {
		finished = true;
		return false;
	}

	if (trecFormat) {
		char docID[256];
		getDocIdForOffset(docID, results[position].from, results[position].to, true);
		sprintf(line, "%s Q0 %s %d %.5f %s",
				queryID, docID, position + 1, results[position].score, runID);
	}
	else {
		printResultLine(line, results[position]);

		if (additionalQuery != NULL)
			addAdditionalStuffToResultLine(line, results[position].from, results[position].to);
		if (getAnnotation)
			addAnnotationToResultLine(line, results[position].from);
		if (printFileName)
			addFileNameToResultLine(line, results[position].from);
		if (printDocumentID) {
			char docID[256];
			getDocIdForOffset(docID, results[position].from, results[position].to, true);
			sprintf(&line[strlen(line)], " \"%s\"", docID);
		}
	}

	position++;
	return true;
} // end of getNextLine(char*)


bool RankedQuery::isValidCommand(const char *command) {
	for (int i = 0; COMMANDS[i] != NULL; i++)
		if (strcasecmp(command, COMMANDS[i]) == 0)
			return true;
	return false;
} // end of isValidCommand(char*)


static void mergeSortResultsByScore(ScoredExtent *results, int count, ScoredExtent *temp) {
	if (count <= 7) {
		for (int i = 0; i < count; i++) {
			int best = i;
			for (int k = i + 1; k < count; k++)
				if (results[k].score > results[best].score)
					best = k;
			temp[0] = results[i];
			results[i] = results[best];
			results[best] = temp[0];
		}
		return;
	}
	int middle = (count >> 1);
	mergeSortResultsByScore(results, middle, temp);
	mergeSortResultsByScore(&results[middle], count - middle, temp);
	int leftPos = 0, rightPos = middle, outPos = 0;
	while (true) {
		if (results[leftPos].score >= results[rightPos].score) {
			temp[outPos++] = results[leftPos++];
			if (leftPos >= middle)
				break;
		}
		else {
			temp[outPos++] = results[rightPos++];
			if (rightPos >= count)
				break;
		}
	}
	while (leftPos < middle)
		temp[outPos++] = results[leftPos++];
	while (rightPos < count)
		temp[outPos++] = results[rightPos++];
	memcpy(results, temp, count * sizeof(ScoredExtent));
} // end of mergeSortResultsByScore(ScoredExtent*, int, ScoredExtent*)


void RankedQuery::sortResultsByScore(ScoredExtent *results, int count, bool inverted) {
	if (count <= 1)
		return;
	ScoredExtent *temp = typed_malloc(ScoredExtent, count + 1);
	mergeSortResultsByScore(results, count, temp);
	free(temp);
	if (inverted)
		for (int i = 0; i < count - 1 - i; i++) {
			ScoredExtent t = results[i];
			results[i] = results[count - 1 - i];
			results[count - 1 - i] = t;
		}
} // end of sortResultsByScore(ScoredExtent*, int, bool)


void RankedQuery::moveLastHeapNodeUp(ScoredExtent *heap, int heapSize) {
	// Attention: the "/ 2" may not be replaced by a ">> 1" here, because the parent
	// of the 0 node is (0-1)/2 == 0. A bit shift would create lots of terror.
	int node = heapSize - 1;
	int parent = (node - 1) / 2;
	while (heap[node].score < heap[parent].score) {
		ScoredExtent sex = heap[node];
		heap[node] = heap[parent];
		heap[parent] = sex;
		node = parent;
		parent = (node - 1) / 2;
	}
} // end of moveLastHeapNodeUp(ScoredExtent*, int)


void RankedQuery::moveFirstHeapNodeDown(ScoredExtent *heap, int heapSize) {
#if 1
	ScoredExtent original = heap[0];
	double score = original.score;
	int node = 0, child = 1;
	while (child + 1 < heapSize) {
		if (heap[child + 1].score < heap[child].score)
			child = child + 1;
		if (heap[child].score >= score)
			break;
		heap[node] = heap[child];
		node = child;
		child = node + node + 1;
	}
	if (child < heapSize)
		if (heap[child].score < score) {
			heap[node] = heap[child];
			node = child;
		}
	heap[node] = original;
#else																	
	int node = 0;
	int leftChild = node + node + 1;
	int rightChild = node + node + 2;
	while (leftChild < heapSize) {
		int child = leftChild;
		if (rightChild < heapSize)
			if (heap[rightChild].score < heap[leftChild].score)
				child = rightChild;
		if (heap[child].score >= heap[node].score)
			break;
		ScoredExtent sex = heap[node];
		heap[node] = heap[child];
		heap[child] = sex;
		node = child;
		leftChild = node + node + 1;
		rightChild = node + node + 2;
	}
#endif
} // end of moveFirstHeapNodeDown(ScoredExtent*, int)


void RankedQuery::processModifiers(const char **modifiers) {
	Query::processModifiers(modifiers);
	char *fbMode = getModifierString(modifiers, "feedback", "");
	feedbackMode = Feedback::FEEDBACK_NONE;
	if (strcasecmp(fbMode, "okapi") == 0)
		feedbackMode = Feedback::FEEDBACK_OKAPI;
	else if (strcasecmp(fbMode, "kld") == 0)
		feedbackMode = Feedback::FEEDBACK_KLD;
	else if (strcasecmp(fbMode, "billerbeck") == 0)
		feedbackMode = Feedback::FEEDBACK_BILLERBECK;
	free(fbMode);

	feedbackTerms = getModifierInt(modifiers, "fbterms", 15);
	feedbackTerms = MIN(100, MAX(feedbackTerms, 1));
	feedbackDocs = getModifierInt(modifiers, "fbdocs", 15);
	feedbackDocs = MIN(100, MAX(feedbackDocs, 1));
	feedbackTermWeight = getModifierDouble(modifiers, "fbweight", 0.3);
	feedbackReweightOrig = getModifierBool(modifiers, "fbreweight", false);
	feedbackStemming = getModifierBool(modifiers, "fbstemming", false);
	feedbackQrels = getModifierString(modifiers, "fbqrels", "");

	if (getModifierBool(modifiers, "rerank", false))
		performReranking = RERANKING_KLD;
	else {
		char *mode = getModifierString(modifiers, "rerank", "");
		if (strcasecmp(mode, "links") == 0)
			performReranking = RERANKING_LINKS;
		else if (strcasecmp(mode, "bayes") == 0)
			performReranking = RERANKING_BAYES;
		else if ((strcasecmp(mode, "kld") == 0) || (strncasecmp(mode, "kld-", 4) == 0)) {
			performReranking = RERANKING_KLD;
			if (strcasecmp(mode, "kld") == 0)
				relevanceModelMethod = RelevanceModel::METHOD_CONCAT;
			else if (strcasecmp(mode, "kld-concat") == 0)
				relevanceModelMethod = RelevanceModel::METHOD_CONCAT;
			else if (strcasecmp(mode, "kld-weighted") == 0)
				relevanceModelMethod = RelevanceModel::METHOD_WEIGHTED;
			else if (strcasecmp(mode, "kld-lavrenko1") == 0)
				relevanceModelMethod = RelevanceModel::METHOD_LAVRENKO_1;
			else if (strcasecmp(mode, "kld-lavrenko2") == 0)
				relevanceModelMethod = RelevanceModel::METHOD_LAVRENKO_2;
			else {
				relevanceModelMethod = RelevanceModel::METHOD_CONCAT;
				log(LOG_ERROR, LOG_ID, "Illegal reranking mode!");
			}
		}
		free(mode);
	}
	getAnnotation = getModifierBool(modifiers, "getAnnotation", false);
	queryID = getModifierString(modifiers, "id", "0");
	runID = getModifierString(modifiers, "runid", "Wumpus");
	char *add = getModifierString(modifiers, "addget", NULL);
	if (add == NULL)
		add = getModifierString(modifiers, "add", NULL);
	else
		addGet = true;
	trecFormat = getModifierBool(modifiers, "trec", false);
	if (trecFormat) {
		getAnnotation = false;
		printDocumentID = true;
		if (add != NULL) {
			free(add);
			add = NULL;
		}
		addGet = false;
	}
	if (add != NULL) {
		additionalQuery =
			new GCLQuery(index, "gcl", EMPTY_MODIFIERS, add, visibleExtents, -1);
		if (!additionalQuery->parse())
			additionalQuery->setResultList(new ExtentList_Empty());
		free(add);
	}
} // end of processModifiers(char**)


const char * RankedQuery::findOutsideQuotationMarks(const char *string, const char *what, bool caseSensitive) {
	if ((what == NULL) || (string == NULL))
		return NULL;
	if ((what[0] == 0) || (string[0] == 0))
		return NULL;
	char c1 = what[0], c2 = what[0];
	int whatLen = strlen(what);
	bool inQuotes = false;
	if ((caseSensitive) && ((c1 | 32) >= 'a') && ((c1 | 32) <= 'z'))
		c2 = (c1 ^ 32);
	while (*string != 0) {
		if (*string == '"')
			inQuotes = !inQuotes;
		else if ((!inQuotes) && ((*string == c1) || (*string == c2))) {
			if (caseSensitive) {
				if (strncmp(string, what, whatLen) == 0)
					return string;
			}
			else if (strncasecmp(string, what, whatLen) == 0)
				return string;
		}
		string++;
	}
	return NULL;
} // end of findOutsideQuotationMarks(char*, char*, bool)


bool RankedQuery::parseQueryString(const char *queryString, const char *defaultContainer,
		const char *defaultStatisticsQuery, int memoryLimit) {
	while (isWhiteSpace(*queryString))
		queryString++;

	const char *by = findOutsideQuotationMarks(queryString, "by", false);
	if (by == NULL) {
		if (defaultContainer != NULL)
			containerQuery =
				new GCLQuery(index, "gcl", EMPTY_MODIFIERS, defaultContainer, visibleExtents, memoryLimit);
	}
	else {
		long length = ((long)by) - ((long)queryString);
		char *container = strncpy((char*)malloc(length + 4), queryString, length);
		container[length] = 0;
		containerQuery =
			new GCLQuery(index, "gcl", EMPTY_MODIFIERS, container, visibleExtents, memoryLimit);
		free(container);
		queryString = &by[2];
		while (isWhiteSpace(*queryString))
			queryString++;
	}

	if (containerQuery != NULL)
		if (!containerQuery->parse())
			return false;

	const char *with = findOutsideQuotationMarks(queryString, "with", false);
	char *scorers = NULL;
	if (with == NULL) {
		if (defaultStatisticsQuery != NULL)
			statisticsQuery =
				new GCLQuery(index, "gcl", EMPTY_MODIFIERS, defaultStatisticsQuery, visibleExtents, memoryLimit);
		scorers = duplicateString(queryString);
	}
	else {
		const char *check = &with[4];
		while (isWhiteSpace(*check))
			check++;
		if (strncasecmp(check, "weights", 7) != 0)
			return false;
		check = &check[7];
		while (isWhiteSpace(*check))
			check++;
		if (strncasecmp(check, "from", 4) != 0)
			return false;
		const char *stats = &check[4];
		statisticsQuery =
			new GCLQuery(index, "gcl", EMPTY_MODIFIERS, stats, visibleExtents, memoryLimit);
		long length = ((long)with) - ((long)queryString);
		scorers = strncpy((char*)malloc(length + 2), queryString, length);
		scorers[length] = 0;
	}

	if (statisticsQuery != NULL)
		if (!statisticsQuery->parse()) {
			free(scorers);
			return false;
		}

	bool result = parseScorers(scorers, memoryLimit);
	free(scorers);
	return result;
} // end of parseQueryString(char*, char*, char*, int)


bool RankedQuery::parseScorers(const char *scorers, int memoryLimit) {
	while (isWhiteSpace(*scorers))
		scorers++;
	QueryTokenizer *tok = new QueryTokenizer(scorers);
	elementCount = originalElementCount = tok->getTokenCount();
	if ((elementCount > MAX_SCORER_COUNT) || (elementCount <= 0)) {
		delete tok;
		return false;
	}

	bool useNGramTokenizer;
	getConfigurationBool("USE_NGRAM_TOKENIZER", &useNGramTokenizer, false);
	if (!useNGramTokenizer) {
		// This is the original strategy: fetch each list from the index
		// individually. It can be improved upon by allowing the index manager
		// to schedule disk I/O in a smarter way.
		for (int i = 0; i < elementCount; i++) {
			char *token = tok->getNext();
			elementQueries[i] =
				createElementQuery(token, &externalWeights[i], memoryLimit);
		}
	}
	else {
		// Transform the token sequence into an n-gram sequence, with individual
		// n-grams joined by a '_' character.
		char tokenSequence[MAX_SCORER_COUNT * (MAX_TOKEN_LENGTH + 1)];
		int lengthOfTokenSequence = 0;
		for (int i = 0; i < elementCount; i++) {
			char *token = tok->getNext();
			tokenSequence[lengthOfTokenSequence++] = '_';
			while ((*token != 0) && (*token != '"'))
				token++;
			while (*token != 0) {
				if (*token != '"')
					tokenSequence[lengthOfTokenSequence++] = *token;
				token++;
			}
		}
		tokenSequence[lengthOfTokenSequence++] = '_';
		int n;
		getConfigurationInt("GRAM_SIZE_FOR_NGRAM_TOKENIZER", &n, 5);
		elementCount = lengthOfTokenSequence - n + 1;
		if (elementCount > MAX_SCORER_COUNT) {
			log(LOG_ERROR, LOG_ID, "Too many ngrams in query. Limiting to MAX_SCORER_COUNT.");
			elementCount = MAX_SCORER_COUNT;
		}

		// Create elementCount fake queries, by sending the n-grams directly to the index.
		char token[MAX_TOKEN_LENGTH + 1];
		int outPos = 0;
		for (int i = 0; i < elementCount; ++i) {
			snprintf(token, n + 1, "%s", &tokenSequence[i]);

#if 0
			bool inner_underscore_found = false;
			for (int k = 1; k < n - 1; k++)
				if (token[k] == '_')
					inner_underscore_found = true;
			if (inner_underscore_found) {
				if (token[0] != '_')
					continue;
				char *c = strchr(token + 1, '_');
				assert(c != NULL);
				c[1] = '*';
				c[2] = 0;
			}
#endif
			printf("%s ", token);

			externalWeights[outPos] = 1.0;
			elementQueries[outPos] =
				new GCLQuery(index, "gcl", EMPTY_MODIFIERS, token, visibleExtents, -1);
			ExtentList *list = index->getPostings(token, userID);
			elementQueries[outPos]->setResultList(list);
			outPos++;
		}
		elementCount = outPos;

		printf("\n");
	}


#if IMPROVED_IO_SCHEDULING
	// New implementation: Allow the index manager to fetch lists in a different
	// order than the one specified by the guy who created this query.
	char *terms[MAX_SCORER_COUNT];
	ExtentList *lists[MAX_SCORER_COUNT];
	bool allSingle = true;

	for (int i = 0; (i < elementCount) && (allSingle); i++) {
		terms[i] = NULL;
		lists[i] = NULL;

		char *toBeFreed = elementQueries[i]->getQueryString();
		char *q = duplicateAndTrim(toBeFreed);
		free(toBeFreed);
		toBeFreed = q;
		int len = strlen(q);
		if ((len < 2) || (len > MAX_TOKEN_LENGTH))
			allSingle = false;
		else if ((q[0] != '"') || (q[len - 1] != '"') || (strchr(q, ' ') != NULL))
			allSingle = false;
		else {
			q[len - 1] = 0;
			terms[i] = duplicateString(&q[1]);
		}
		free(toBeFreed);
	}

	// fetch lists from on-disk indices; allow index manager to decide in
	// which order lists are fetched from disk
	if (allSingle)
		index->getPostings(terms, elementCount, userID, lists);

	// replace original data in elementQueries by data just fetched from index
	for (int i = 0; i < elementCount; i++) {
		if (terms[i] != NULL) {
			free(terms[i]);
			terms[i] = NULL;
		}
		if (lists[i] != NULL)
			elementQueries[i]->setResultList(lists[i]);
	}
#endif  // IMPROVED_IO_SCHEDULING

	for (int i = 0; i < elementCount; i++) {
		elementQueries[i]->almostSecureWillDo();
		if (!elementQueries[i]->parse())
			return false;
	}
	delete tok;
	return (elementCount > 0);
} // end of scorers(char*)


GCLQuery * RankedQuery::createElementQuery(const char *query, double *weight, int memoryLimit) {
	if (query == NULL)
		return NULL;
	while (isWhiteSpace(*query))
		query++;
	if (*query == '#') {
		query++;
		int i = 0;
		while (query[i] > ' ')
			i++;
		char *w = strncpy((char*)malloc(i + 2), query, i);
		w[i] = 0;
		if (sscanf(w, "%lf", weight) == 0) {
			free(w);
			return NULL;
		}
		free(w);
		if (*weight > MAX_QTW) {
			*weight = MAX_QTW;
			snprintf(errorMessage, sizeof(errorMessage),
					"Upper limit for query term weight exceeded: %s", query);
			log(LOG_DEBUG, LOG_ID, errorMessage);
		}
		query = &query[i];
	}
	else
		*weight = 1.0;
	return new GCLQuery(index, "gcl", EMPTY_MODIFIERS, query, visibleExtents, memoryLimit);
} // end of createElementQuery(char*, double*, int)


void RankedQuery::computeTermCorpusWeights() {
	double corpusSize = 0.0;
	if (visibleExtents != NULL) {
		// if we have a list of visible extents, get total corpus size from there
		ExtentList *list = visibleExtents->getExtentList();
		corpusSize = list->getTotalSize();
		delete list;
	}
	else {
		// if not, assume the corpus starts at the first occurrence of a query
		// term and ends at the last occurrence of a query term
		offset corpusStart = MAX_OFFSET, corpusEnd = -1;
		offset start, end;
		for (int i = 0; i < elementCount; i++) {
			if (elementQueries[i]->getResult()->getFirstStartBiggerEq(0, &start, &end))
				if (start < corpusStart)
					corpusStart = start;
			if (elementQueries[i]->getResult()->getLastEndSmallerEq(MAX_OFFSET, &start, &end))
				if (end > corpusEnd)
					corpusEnd = end;
		}
		corpusSize = (corpusEnd - corpusStart + 1);
	}

	if (corpusSize < 1)
		corpusSize = 1;

	// assign element weights
	for (int i = 0; i < elementCount; i++) {
		offset len = elementQueries[i]->getResult()->getLength();
		if (len < 1)
			internalWeights[i] = 0.0;
		else if (len >= corpusSize)
			internalWeights[i] = 0.0;
		else
			internalWeights[i] = externalWeights[i] * log(corpusSize / len) / LOG_2;
	}
} // end of computeTermCorpusWeights()


void RankedQuery::addToResultSet(ScoredExtent *candidate, int *resultCount) {
	// we have a heap structure that contains the best "count" containers;
	// if the heap is not full yet, simply add the new result
	int rc = *resultCount;
	if (rc < count) {
		results[rc] = *candidate;
		if (++rc >= count)
			sortResultsByScore(results, rc, true);
		*resultCount = rc;
	}
	else if (candidate->score > results[0].score) {
		// otherwise, replace the min-scoring member of the heap by the new
		// result candidate and restore heap property
		results[0] = *candidate;
		moveFirstHeapNodeDown(results, rc);
	}
} // end of addToResultSet(ScoredExtent*, int*)


ExtentList * RankedQuery::getListForGCLExpression(const char *expression) {
	GCLQuery q(index, "gcl", EMPTY_MODIFIERS, expression, visibleExtents, -1);
	if (!q.parse())
		return NULL;
	else {
		ExtentList *result = q.resultList;
		q.resultList = NULL;
		return result;
	}
} // end of getListForGCLExpression(char*)	


LanguageModel * RankedQuery::getLanguageModelFromTopResults(int docCount, bool withStemming) {
	LanguageModel *result = new LanguageModel(0, 0, withStemming);
	for (int i = 0; (i < docCount) && (i < count); i++) {
		LanguageModel *model =
			new LanguageModel(index, results[i].from, results[i].to, withStemming);
		result->addLanguageModel(model);
		delete model;
	}
	return result;
} // end of getLanguageModelFromTopResults(int, bool)


static bool stemEquiv(char *t1, char *t2) {
	int inPos, outPos;
	char T1[MAX_TOKEN_LENGTH * 2];
	char T2[MAX_TOKEN_LENGTH * 2];
	outPos = 0;
	for (inPos = 0; t1[inPos] != 0; inPos++)
		if (((t1[inPos] | 32) >= 'a') && ((t1[inPos] | 32) <= 'z'))
			T1[outPos++] = (t1[inPos] | 32);
	T1[outPos] = 0;
	outPos = 0;
	for (inPos = 0; t2[inPos] != 0; inPos++)
		if (((t2[inPos] | 32) >= 'a') && ((t2[inPos] | 32) <= 'z'))
			T2[outPos++] = (t2[inPos] | 32);
	T2[outPos] = 0;
	Stemmer::stem(T1, LANGUAGE_ENGLISH, false);
	Stemmer::stem(T2, LANGUAGE_ENGLISH, false);
	return (strcmp(T1, T2) == 0);
} // end of stemEquiv(char*, char*)


void RankedQuery::feedback(int docCount, int termCount, bool withStemming) {
	if ((count <= 1) || (docCount <= 1) || (termCount <= 0))
		return;
	if (docCount > count)
		docCount = count;
	if (termCount > 50)
		termCount = 50;

	// obtain list of query term strings (needed by Okapi feedback to give greater
	// weight to original terms than to expansion terms)
	char *queryTermStrings[MAX_SCORER_COUNT];
	for (int i = 0; i < elementCount; i++) {
		queryTermStrings[i] = elementQueries[i]->getQueryString();
		replaceChar(queryTermStrings[i], '"', ' ', true);
		trimString(queryTermStrings[i]);
		toLowerCase(queryTermStrings[i]);
	}

	// perform actual feedback
	Feedback *fb = new Feedback(index, withStemming);
	offset *start, *end;
	if ((feedbackQrels != NULL) && (feedbackQrels[0] != 0)) {
		// Perform actual relevance feedback, using pre-existing qrels.
		Qrels qrels(feedbackQrels);
		std::vector<std::string> relevant_docids;
		qrels.getRelevantDocuments(queryID, &relevant_docids);
		docCount = relevant_docids.size();
		start = typed_malloc(offset, docCount + 1);
		end = typed_malloc(offset, docCount + 1);
		for (int i = 0; i < docCount; i++) {
			const string& docid = relevant_docids[i];
			log(LOG_DEBUG, LOG_ID, string("Using feedback information from relevant document: ") + docid);
			const string gclQuery =
				string("(\"<doc>\"..\"</doc>\")>((\"<docno>\"..\"</docno>\")>\"") +	docid + "\")";
			ExtentList *document = getListForGCLExpression(gclQuery.c_str());
			assert(document != NULL);
			if (!document->getFirstStartBiggerEq(-1, &start[i], &end[i])) {
				log(LOG_ERROR, LOG_ID, string("Unable to find document for docid: ") + docid);
				start[i] = end[i] = 0;
			}
			delete document;
		}
	}
	else {
		// Perform pseudo-relevance feedback, using the top documents from the
		// initial retrieval phase.
		start = typed_malloc(offset, docCount);
		end = typed_malloc(offset, docCount);
		for (int i = 0; i < docCount; i++) {
			start[i] = results[i].from;
			end[i] = results[i].to;
		}
	}
	FeedbackScore *terms = typed_malloc(FeedbackScore, 100);
	fb->doFeedback(feedbackMode,
			start, end, docCount, queryTermStrings, elementCount, terms, 100);
	delete fb;

	// free memory for copies of query term strings
	for (int i = 0; i < elementCount; i++)
		free(queryTermStrings[i]);

	// add new terms to query, avoiding duplicates
	originalElementCount = elementCount;
	if (terms[0].score > 0) {
		for (int i = 0; (i < termCount + 20) &&
				(elementCount < originalElementCount + termCount) && (terms[i].score > 0); i++) {
			bool duplicateFound = false;
			for (int k = elementCount - 1; (k >= 0) && (!duplicateFound); k--) {
				char *q = elementQueries[k]->getQueryString();
				replaceChar(q, '"', ' ', true);
				trimString(q);
				if (strcasecmp(q, terms[i].term) == 0)
					duplicateFound = true;
				else if ((withStemming) || (strchr(terms[i].term, '$') != NULL))
					if (stemEquiv(q, terms[i].term))
						duplicateFound = true;
				if ((duplicateFound) && (feedbackReweightOrig) && (k < originalElementCount)) {
					// adjust term weight of original query term
					externalWeights[k] = MAX(externalWeights[k], terms[i].weight);
				}
				free(q);
			}
			if ((!duplicateFound) && (elementCount < MAX_SCORER_COUNT)) {
				// add new term to query
				char term[MAX_TOKEN_LENGTH * 2];
				externalWeights[elementCount] = terms[i].weight * feedbackTermWeight;
				sprintf(term, "\"%s%s\"", (withStemming ? "$" : ""), terms[i].term);
				elementQueries[elementCount] =
					new GCLQuery(index, "gcl", EMPTY_MODIFIERS, term, visibleExtents, -1);
				if (!elementQueries[elementCount]->parse())
					delete elementQueries[elementCount];
				else
					elementCount++;
			}
		}
	} // end if (terms[0].score > 0)

	assert(elementCount <= originalElementCount + termCount);

	// print information about expansion terms to log file and to output
	// (if verbose mode requested)
	int fbCnt = elementCount - originalElementCount;
	char *debugString = (char*)malloc(256 * fbCnt + 256);
	int len = sprintf(debugString, "Adding %d feedback terms to query", fbCnt);
	for (int i = 0; i < fbCnt; i++) {
		char *q = elementQueries[i + originalElementCount]->getQueryString();
		len += sprintf(&debugString[len], "%s %s (%.4lf)",
				(i == 0 ? ":" : ","), q, externalWeights[i + originalElementCount]);
		free(q);
	}
	log(LOG_DEBUG, LOG_ID, debugString);
	if (verbose)
		addVerboseString(NULL, debugString);
	free(debugString);

	// release temporary resources
	free(start);
	free(end);
	free(terms);
} // end of feedback(int, int, bool)


ScoredExtent RankedQuery::getResult(int i) {
	if ((i >= 0) && (i < count))
		return results[i];
	else
		return results[0];
} // end of getResult(int)


void RankedQuery::rerankResultsKLD(int docCount, double weight, int method) {
	if ((docCount <= 1) || (count <= 1))
		return;

	// transform search results into a format understood by the RelevanceModel class
	offset *docStarts = typed_malloc(offset, docCount);
	offset *docEnds = typed_malloc(offset, docCount);
	double *docScores = typed_malloc(double, docCount);
	for (int i = 0; i < docCount; i++) {
		docStarts[i] = results[i].from;
		docEnds[i] = results[i].to;
		docScores[i] = results[i].score;
	}

	// grab copies of all query terms
	char **queryTerms = typed_malloc(char*, elementCount);
	for (int i = 0; i < elementCount; i++) {
		char *q = elementQueries[i]->getQueryString();
		for (char *p = q; *p != 0; p++)
			if ((*p > 0) && (*p < '0'))
				*p = ' ';
		queryTerms[i] = duplicateAndTrim(q);
		free(q);
	}

	// build relevance model from query terms and top documents
	RelevanceModel *relevanceModel =
		new RelevanceModel(
				index, docStarts, docEnds, docScores, docCount, queryTerms, elementCount, method);
	free(docStarts);
	free(docEnds);
	free(docScores);

	double ALPHA = 1;
	double BETA = weight;
	LanguageModel *backgroundModel = index->getStaticLanguageModel();

	bool done = false;
#if 1
	assert(!done);
	double klds[10000];
	double minScore = +1E9, maxScore = -1E9;
	double minKLD = +1E9, maxKLD = -1E9;
	for (int i = 0; i < count; i++) {
		LanguageModel *model =
			new LanguageModel(index, results[i].from, results[i].to, true);
		double kld = LanguageModel::getKLD(model, relevanceModel, backgroundModel);
//		double kld = LanguageModel::getKLD(relevanceModel, model, backgroundModel);
		double backgroundKLD = LanguageModel::getKLD(model, backgroundModel, backgroundModel);
		kld = kld / backgroundKLD;
		if (results[i].score > maxScore) maxScore = results[i].score;
		if (results[i].score < minScore) minScore = results[i].score;
		if (kld > maxKLD) maxKLD = kld;
		if (kld < minKLD) minKLD = kld;
		klds[i] = kld;
		delete model;
	}
	if (maxScore == minScore)
		maxScore += 1E-9;
	if (maxKLD == minKLD)
		maxKLD += 1E-9;
	for (int i = 0; i < count; i++) {
//		results[i].score = (results[i].score - minScore) / (maxScore - minScore)
//			               - (klds[i] - minKLD) / (maxKLD - minKLD);
		results[i].score = ALPHA * results[i].score - BETA * klds[i];
	}
	done = true;
#endif
#if 0
	assert(!done);
	map<string,double> relModel;
	double referenceScore = 0;
	for (int k = 0; k < relevanceModel->getTermCount(); k++) {
		char *term = (char*)relevanceModel->getTermString(k);
		double q = relevanceModel->getTermProbability(term);
		double c = backgroundModel->getTermProbability(term);
		if ((q > c) && (c > 0)) {
			relModel[term] = q;
			referenceScore = q * log(q / c);
		}
		free(term);
	}
	for (int i = 0; i < count; i++) {
		LanguageModel *model =
			new LanguageModel(index, results[i].from, results[i].to, true);
		double kldScore = 0;
		map<string,double>::iterator relModelIter;
		for (relModelIter = relModel.begin(); relModelIter != relModel.end(); ++relModelIter) {
			char *term = (char*)relModelIter->first.c_str();
			double p = model->getTermProbability(term);
			double q = relModelIter->second;
			double c = backgroundModel->getTermProbability(term);
			if ((p > 0) && (c > 0))
				kldScore += q * log(p / c);
		}
		kldScore /= referenceScore;
		results[i].score = ALPHA * results[i].score + BETA * kldScore;
		delete model;
	}
	done = true;
#endif
	assert(done);

	// free all memory allocated
	for (int i = 0; i < elementCount; i++)
		free(queryTerms[i]);
	free(queryTerms);
	delete relevanceModel;

	// satinize scores (make sure we don't have any negative scores) and re-sort
	// all documents by their new score
	for (int i = 0; i < count; i++)
		if (results[i].score < 1.0)
			results[i].score = 1 / (2 - results[i].score);
	sortResultsByScore(results, count, false);
} // end of rerankResultsKLD(int, double, int)


void RankedQuery::rerankResultsBayes(int docCount) {
	if ((docCount <= 1) || (count <= 1))
		return;

	LanguageModel *collectionModel = index->getStaticLanguageModel();

	// A mapping from term to Pr[rel|term]. We assume that the top documents
	// are relevant.
	map<string, double> termProbabilities;
	for (int i = 0; (i < count) && (i < docCount); i++) {
		LanguageModel documentModel(index, results[i].from, results[i].to, false);
		for (int t = 0; t < documentModel.getTermCount(); ++t) {
			char *term = documentModel.getTermString(t);
			offset tf, df;
			collectionModel->getTermInfo(term, &tf, &df);
			if (df > 1)
				termProbabilities[term] += 1.0 / collectionModel->documentCount;
			free(term);
		}
	}

	const double probRel = min(count, docCount) / collectionModel->documentCount;

	// Compute probabilities according to Naive Bayes.
	vector<double> docProbabilities;
	double maxP = 0.0;
	for (int i = 0; i < count; i++) {
		LanguageModel documentModel(index, results[i].from, results[i].to, false);
		double logodds = 0.0;
		for (map<string, double>::const_iterator iter = termProbabilities.begin();
		     iter != termProbabilities.end(); ++iter) {
			offset tf, df;
			documentModel.getTermInfo(iter->first.c_str(), &tf, &df);
			double p = iter->second;
			if (df <= 0)
				p = 1.0 / collectionModel->documentCount;
			logodds += log(p / (1.0 - p));
		}
		logodds /= termProbabilities.size();
		const double p = 1.0 / (1.0 + exp(-logodds));
		docProbabilities.push_back(p);
		if (p > maxP)
			maxP = p;
	}

	// Adjust all document scores based on their predicted relevance according
	// to our naive Bayes classifier.
	double totalWeight = 0.0;
	for (int i = 0; i < elementCount; i++)
		totalWeight += internalWeights[i];
	for (int i = 0; i < count; i++) {
		const double normalizedProb = docProbabilities[i] / maxP;
		results[i].score = results[i].score + normalizedProb * totalWeight;
	}

  sortResultsByScore(results, count, false);
}


void RankedQuery::rerankResultsLinks(int docCount) {
	// maximum number of documents to include in reranking operation
	static const int MAX_RERANK_COUNT = 200;

	// decay factor used to adjust weights of individual documents; document
	// at rank i has weight score(i) * DECAY**i
	static const double DECAY = 0.98;

	// relative weight of anchors in reranking process
	static const double ANCHOR_TERM_WEIGHT = 0.9;

	// relative weight of links in reranking process
	static const double LINK_WEIGHT = 1.0 - ANCHOR_TERM_WEIGHT;

	static const double FULL_MATCH_BOOST = 1.0;
	static const double EXACT_MATCH_BOOST = 1.3;

	docCount = MIN(docCount, MIN(count, MAX_RERANK_COUNT));
	if (docCount <= 1)
		return;

	// extract URLs and base addresses (BASE HREF=...)
	string docID, url, base;
	vector<string> baseURLs;
	vector<string> urls;
	vector<string> hosts;
	vector< pair<string,string> > links;
	map<string,int> url2rank;

	bool isTrecData =
		DocumentAnalyzer::analyzeTRECHeader(
				index, results[0].from, results[0].to, &docID, &url, &base);
	if (url.empty())
		isTrecData = false;

	for (int i = 0; i < docCount; i++) {
		bool status;
		if (isTrecData)
			status = DocumentAnalyzer::analyzeTRECHeader(
					index, results[i].from, results[i].to, &docID, &url, &base);
		else
			status = DocumentAnalyzer::analyzeWikipediaPage(
					index, results[i].from, results[i].to, &docID, &url, &links);
		if (!status) {
			baseURLs.push_back("");
			urls.push_back("");
			hosts.push_back("");
			continue;
		}
		char *u = duplicateString(url.c_str());
		if (isTrecData)
			normalizeURL(u);
		url2rank[u] = i;
		urls.push_back(u);
		baseURLs.push_back(base);
		string host = "";
		for (int k = 0; u[k] != 0; k++)
			if (u[k] == '/')
				break;
			else
				host += u[k];
		hosts.push_back(host);
		free(u);
	}

	// collect all query terms and their internal weights, after
	// applying IDF scoring etc.
	map<string,int> queryTerms;
	double totalQueryTermWeight = 0;
	for (int i = 0; i < elementCount; i++) {
		char *term = elementQueries[i]->getQueryString();
		normalizeString(term);
		queryTerms[term] = i;
		char stemmed[2 * MAX_TOKEN_LENGTH];
		Stemmer::stemWord(term, stemmed, LANGUAGE_ENGLISH, false);
		if (stemmed[0] != 0) {
			strcat(stemmed, "$");
			queryTerms[stemmed] = i;
		}
		totalQueryTermWeight += internalWeights[i];
		free(term);
	}

	double topScore = results[0].score;
	double rerankScores[MAX_RERANK_COUNT];
	for (int i = 0; i < docCount; i++)
		rerankScores[i] = 0;

	// for each document, extract its outgoing links; update votes for documents,
	// based on query terms found in the anchor text
	for (int i = 0; i < docCount; i++) {
		// set containing (document, query term) pairs
		set<int> termAssociations;
		set<int> targetDocuments;

		string dummy;
		vector< pair<string,string> > links;
		bool status;
		if (isTrecData) {
			if (baseURLs[i].empty())
				continue;
			status = DocumentAnalyzer::extractLinks(
					index, results[i].from, results[i].to, &links);
		}
		else
			status = DocumentAnalyzer::analyzeWikipediaPage(
					index, results[i].from, results[i].to, &dummy, &dummy, &links);
		if ((!status) || (links.size() == 0))
			continue;

		map<int, vector<string> > anchorsForDocument;
	
		// follow all links in this document and collect the votes associated
		// with them; also make a note about which query terms are used in the link
		for (unsigned int k = 0; k < links.size(); k++) {
			string l;
			if (isTrecData) {
				char *link = evaluateRelativeURL(baseURLs[i].c_str(), links[k].first.c_str());
				normalizeURL(link);
				l = link;
				free(link);
			}
			else
				l = links[k].first;
			if (url2rank.find(l) == url2rank.end())
				continue;
			int target = url2rank[l];
			if (target == i)
				continue;

			string anchor = links[k].second;
			normalizeString(&anchor);
			if (anchorsForDocument.find(target) == anchorsForDocument.end())
				anchorsForDocument[target] = vector<string>();
			anchorsForDocument[target].push_back(anchor);
		}

		map<int, vector<string> >::iterator iter;
		for (iter = anchorsForDocument.begin(); iter != anchorsForDocument.end(); ++iter) {
			int target = iter->first;
			const vector<string> &anchors = iter->second;
			double documentWeight = results[i].score / topScore * pow(DECAY, i);

			bool fullMatch = false, exactMatch = false;
			set<int> queryTermsSeen;
			for (unsigned int k = 0; k < anchors.size(); k++) {
				set<string> termsInAnchor;
				set<int> queryTermsInAnchor;
				StringTokenizer tok(anchors[k].c_str(), " ");
				for (char *token = tok.nextToken(); token != NULL; token = tok.nextToken()) {
					termsInAnchor.insert(token);
					if (queryTerms.find(token) != queryTerms.end())
						queryTermsInAnchor.insert(queryTerms[token]);
					char stemmed[2 * MAX_TOKEN_LENGTH];
					Stemmer::stemWord(token, stemmed, LANGUAGE_ENGLISH, false);
					if (stemmed[0] != 0) {
						strcat(stemmed, "$");
						if (queryTerms.find(stemmed) != queryTerms.end())
							queryTermsInAnchor.insert(queryTerms[stemmed]);
					}
				}
				if (queryTermsInAnchor.size() > 0) {
					queryTermsSeen.insert(queryTermsInAnchor.begin(), queryTermsInAnchor.end());
					if (queryTermsInAnchor.size() == elementCount) {
						if (termsInAnchor.size() == queryTermsInAnchor.size())
							exactMatch = true;
						else
							fullMatch = true;
					}
				}
			}
			double anchorScore = 0;
			for (set<int>::iterator iter = queryTermsSeen.begin(); iter != queryTermsSeen.end(); ++iter)
				anchorScore += internalWeights[*iter] / totalQueryTermWeight;
			anchorScore = pow(anchorScore, 2);
			if (exactMatch)
				anchorScore *= EXACT_MATCH_BOOST;
			else if (fullMatch)
				anchorScore *= FULL_MATCH_BOOST;

			// update reranking score for target document
			rerankScores[target] += anchorScore * ANCHOR_TERM_WEIGHT * documentWeight;
			rerankScores[target] += LINK_WEIGHT * documentWeight;
		}
	} // end for (int i = 0; i < docCount; i++)

	sprintf(errorMessage, "Reranking finished for topic %s", queryID);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	int best = 0;
	for (int i = 1; i < docCount; i++)
		if (rerankScores[i] > rerankScores[best])
			best = i;
	snprintf(errorMessage, sizeof(errorMessage),
	         "Navigational result: %s (score = %.2lf)", urls[best].c_str(), rerankScores[best]);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	// adjust scores and rerank top "docCount" results
	for (int i = 0; i < docCount; i++)
		results[i].score *= 1 + log(1 + rerankScores[i]);
	sortResultsByScore(results, docCount, false);
} // rerankResultsLinks(int)


void RankedQuery::analyzeKLD() {
	if (count == 0)
		return;
	FeedbackScore *terms = typed_malloc(FeedbackScore, 10000);
	Feedback *fb = new Feedback(index, true);
	printf("# QUERY_ID DOC_RANK TERM_RANK TERM_COUNT QUERY_TERM SCORE\n");
	for (int i = 0; i < count; i++) {
		fb->doFeedback(Feedback::FEEDBACK_KLD,
				&results[i].from, &results[i].to, 1, NULL, 0, terms, 10000);
		int termCount = 0;
		while (termCount < 9999) {
			if (terms[termCount].term[0] != 0)
				termCount++;
			else
				break;
		}
		for (int k = 0; k < termCount; k++) {
			for (int l = 0; l < elementCount; l++) {
				char *q = elementQueries[l]->getQueryString();
				if (stemEquiv(q, terms[k].term))
					printf("# KLD: %s %d %d %d %s %.5lf\n", queryID, i + 1, k + 1, termCount, q, terms[k].score);
				free(q);
			}
		}
	}
	delete fb;
	free(terms);
} // end of analyzeKLD()


int RankedQuery::getType() {
	return QUERY_TYPE_RANKED;
}


void RankedQuery::getCorpusStatistics(
		offset *corpusSize, offset *documentCount, offset *scorerFreq, offset *scorerDF) {
	ExtentList *statsList = statisticsQuery->getResult();
	assert(statsList != NULL);

	offset cSize = 0, dCount = 0;
	memset(scorerFreq, 0, elementCount * sizeof(offset));
	memset(scorerDF, 0, elementCount * sizeof(offset));

	*corpusSize = cSize;
	*documentCount = dCount;
} // end of getCorpusStatistics(double*, double*, double*, double*)


/** Simple comparator function, to be used when sorting with qsort. **/
int extentScoreComparator(const void *a, const void *b) {
	ScoredExtent *x = (ScoredExtent*)a;
	ScoredExtent *y = (ScoredExtent*)b;
	if (x->score > y->score)
		return -1;
	else if (x->score < y->score)
		return +1;
	else
		return 0;
} // end of extentScoreComparator(const void*, const void*)


int invertedExtentScoreComparator(const void *a, const void *b) {
	ScoredExtent *x = (ScoredExtent*)a;
	ScoredExtent *y = (ScoredExtent*)b;
	if (x->score < y->score)
		return -1;
	else if (x->score > y->score)
		return +1;
	else
		return 0;
} // end of invertedExtentScoreComparator(const void*, const void*)


int extentOffsetComparator(const void *a, const void *b) {
	ScoredExtent *x = (ScoredExtent*)a;
	ScoredExtent *y = (ScoredExtent*)b;
	if (x->from != y->from) {
		if (x->from < y->from)
			return -1;
		else
			return +1;
	}
	else if (x->to < y->to)
		return -1;
	else if (y->to < x->to)
		return +1;
	else
		return 0;
} // end of extentOffsetComparator(const void*, const void*)


int offsetComparator(const void *a, const void *b) {
	offset *x = (offset*)a;
	offset *y = (offset*)b;
	if (*x < *y)
		return -1;
	else if (*x > *y)
		return +1;
	else
		return 0;
} // end of offsetComparator(const void*, const void*)


