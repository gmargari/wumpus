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
 * Implementation of the DesktopQuery ranking algorithm.
 *
 * author: Stefan Buettcher
 * created: 2005-03-17
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <math.h>
#include <string.h>
#include "desktopquery.h"
#include "getquery.h"
#include "qapquery.h"
#include "querytokenizer.h"
#include "../misc/all.h"
#include "../stemming/stemmer.h"
#include "../terabyte/terabyte_query.h"


static const char * CONTAINER_STRING = "\"<document!>\"..\"</document!>\"";

static const char * LOG_ID = "DesktopQuery";

const double DesktopQuery::DEFAULT_B;
const double DesktopQuery::DEFAULT_K1;


DesktopQuery::DesktopQuery(Index *index, const char *command, const char **modifiers,
		const char *body, uid_t userID, int memoryLimit) {
	this->index = index;
	this->userID = userID;
	this->memoryLimit = memoryLimit;
	if (index->APPLY_SECURITY_RESTRICTIONS)
		visibleExtents = index->getVisibleExtents(userID, false);
	else
		visibleExtents = index->getVisibleExtents(Index::GOD, false);
	mustFreeVisibleExtentsInDestructor = true;

	ok = false;
	position = 0;
	resultStart = 0;
	resultEnd = 9;
	pageNumberList = NULL;
	noIDF = false;
	processModifiers(modifiers);

	// copy data for later processing
	queryString = duplicateString(body);
	pageNumberList = getPostings("<newpage/>", Index::GOD);
} // end of DesktopQuery(...)


DesktopQuery::~DesktopQuery() {
	if (pageNumberList != NULL) {
		delete pageNumberList;
		pageNumberList = NULL;
	}
} // end of ~DesktopQuery()


/** A DesktopQuery basically just looks like @desktop "term1", "term2", ... **/
bool DesktopQuery::parse() {
	if (!parseQueryString(queryString, CONTAINER_STRING, NULL, memoryLimit)) {
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
} // end of parse()


static const int PREVIEW = 64;

/**
 * The LHS ("ListHeapStruct") structure is used to "merge" the individual
 * document-level postings lists and process the resulting document stream.
 **/
typedef struct {
	int who;
	offset next;
	int previewPos;
	int previewCount;
	offset preview[PREVIEW];
} LHS;

static int lhsComparator(const void *a, const void *b) {
	LHS *x = *((LHS**)a);
	LHS *y = *((LHS**)b);
	offset difference = x->next - y->next;
	if (difference < 0)
		return -1;
	else if (difference > 0)
		return +1;
	else
		return 0;
} // end of lhsComparator(const void*, const void*)


#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)


void DesktopQuery::processCoreQueryDocLevel() {
	const char *getMod[2] = {"filtered", NULL};
	char body[256];
	char *mod[2];
	mod[0] = (char*)malloc(32);
	sprintf(mod[0], "count=%d", count);
	mod[1] = NULL;

	char *qs = queryString;
	if (strstr(qs, " by ") != NULL)
		qs = strstr(qs, " by ") + 4;
	if (strstr(qs, " with ") != NULL)
		*strstr(qs, " with ") = 0;

	TerabyteQuery *q = new TerabyteQuery(index, "bm25tera", (const char**)mod, qs, (VisibleExtents*)NULL, -1);
	if (!q->parse()) {
		results = typed_malloc(ScoredExtent, 1);
		count = 0;
	}
	else {
		count = q->getCount();
		results = typed_malloc(ScoredExtent, count);
		for (int i = 0; i < count; i++) {
			results[i] = q->getResult(i);
			results[i].containerFrom = results[i].from;
			results[i].containerTo = results[i].to;
			if ((i < resultStart) || (i > resultEnd))
				continue;
			double maxProxiScore = 0.0;
			sprintf(body, OFFSET_FORMAT " " OFFSET_FORMAT, results[i].from, results[i].to);

			// construct an @get query for the current result extent and use it to
			// find a good passage to return
			GetQuery *gq = new GetQuery(index, "get", getMod, body, visibleExtents, -1);
			if (!gq->parse())
				results[i].to = MIN(results[i].from + 32, results[i].to);
			else {
				char buffer[FilteredInputStream::MAX_FILTERED_RANGE_SIZE + 32];
				if (!gq->getNextLine(buffer))
					results[i].to = MIN(results[i].from + 32, results[i].to);
				else {
					char *queryTerms[MAX_SCORER_COUNT];
					offset lastForTerm[MAX_SCORER_COUNT];
					for (int k = 0; k < elementCount; k++) {
						queryTerms[k] = elementQueries[k]->getQueryString();
						int outPos = 0;
						for (int inPos = 0; queryTerms[k][inPos] != 0; inPos++)
							if ((queryTerms[k][inPos] <= 0) || (queryTerms[k][inPos] > '"'))
								queryTerms[k][outPos++] = queryTerms[k][inPos];
						queryTerms[k][outPos] = 0;
						lastForTerm[k] = -999999999;
					}
					offset pos = results[i].from;

					// tokenize the result of the @get query and try to find a good passage within it
					StringTokenizer *tok = new StringTokenizer(buffer, " ");
					char *token;
					while ((token = tok->getNext()) != NULL) {
						for (int k = 0; k < elementCount; k++)
							if (Stemmer::stemEquivalent(token, queryTerms[k], LANGUAGE_ENGLISH))
								lastForTerm[k] = pos;
						offset start = pos;
						double score = 0.0;
						for (int t = 0; t < elementCount; t++)
							if (lastForTerm[t] >= pos - 12) {
								start = MIN(lastForTerm[t], start);
								score += internalWeights[t] + 100 - (pos - lastForTerm[t]);
							}
						if (score > maxProxiScore) {
							maxProxiScore = score;
							results[i].from = start;
							results[i].to = pos;
						}
						pos++;
					}
					delete tok;

					results[i].to = MIN(results[i].to, results[i].from + 12);
					for (int k = 0; k < elementCount; k++)
						free(queryTerms[k]);
				}
			} // end else [gq->parse() == true]

			delete gq;
		}
	} // end else [q->parse() == true]

	delete q;
} // end of exexuteQueryDocLevel()


void DesktopQuery::processCoreQuery() {
	if (index->DOCUMENT_LEVEL_INDEXING > 1)
		processCoreQueryDocLevel();

	offset start, end, s, e;
	offset positiveContainerCount[MAX_SCORER_COUNT];
	offset nextPossibleForElement[MAX_SCORER_COUNT];
	ExtentList *elementLists[MAX_SCORER_COUNT];

	ExtentList *containerList = containerQuery->getResult();
	ExtentList *statisticsList = statisticsQuery->getResult();

	// compute containerCount and avgContainerLength;
	// count positive and negative containers for all query elements
	offset containerCount = 0;
	offset avgContainerLength = 0;

	offset nextOffsetPossible = MAX_OFFSET;
	for (int elem = 0; elem < elementCount; elem++) {
		elementLists[elem] = elementQueries[elem]->getResult();
		if (elementLists[elem]->getFirstEndBiggerEq(0, &start, &end)) {
			nextPossibleForElement[elem] = end;
			if (nextPossibleForElement[elem] < nextOffsetPossible)
				nextOffsetPossible = nextPossibleForElement[elem];
		}
		else
			nextPossibleForElement[elem] = MAX_OFFSET;
		positiveContainerCount[elem] = 0;
	} // end for (int elem = 0; elem < elementCount; elem++)

	// scan over all containers to compute collection statistics; use getNextN to
	// reduce the number of virtual method calls (getFirstStartBiggerEq)
	offset nextPossible = 0;
	offset containerPreviewStart[64], containerPreviewEnd[64];
	int previewSize = statisticsList->getNextN(0, MAX_OFFSET, 64,
				containerPreviewStart, containerPreviewEnd);
	int previewPos = 0;
	while (previewPos < previewSize) {
		start = containerPreviewStart[previewPos];
		end = containerPreviewEnd[previewPos];
		avgContainerLength += (end - start + 1);
		containerCount++;
		if (end >= nextPossible) {
			nextPossible = MAX_OFFSET;
			for (int elem = 0; elem < elementCount; elem++) {
				if (nextPossibleForElement[elem] <= end) {
					if (elementLists[elem]->getFirstStartBiggerEq(start, &s, &e)) {
						if (e <= end)
							positiveContainerCount[elem]++;
						else
							nextPossibleForElement[elem] = e;
					}
					else
						nextPossibleForElement[elem] = MAX_OFFSET;
				}
				if (nextPossibleForElement[elem] < nextPossible)
					nextPossible = nextPossibleForElement[elem];
			}
		}
		if (++previewPos >= previewSize) {
			if (previewSize == 64) {
				previewPos = 0;
				previewSize = statisticsList->getNextN(containerPreviewStart[previewSize - 1] + 1,
						MAX_OFFSET, 64, containerPreviewStart, containerPreviewEnd);
			}
		}
	} // end while (containerList->getFirstStartBiggerEq(containerPosition, &start, &end))

	if (containerCount == 0) {
		// no matching container found: stop execution
		count = 0;
		return;
	}

	double averageContainerLength = avgContainerLength;
	averageContainerLength /= containerCount;

	if (!noIDF) {
		// compute the BM25 term weight for all elements
		for (int i = 0; i < elementCount; i++) {
			double df = positiveContainerCount[i];
			if ((df < 1) || (df > containerCount - 1))
				internalWeights[i] = 0;
			else
				internalWeights[i] = externalWeights[i] * log(containerCount / df);
		}
	} // end if (!noIDF)

	// local variables for term proximity scoring
	int whichScorer[MAX_SCORER_COUNT];
	unsigned int tf[MAX_SCORER_COUNT];
	float proxiScore[MAX_SCORER_COUNT];
	char *areTheSame = NULL;
	offset **occ = NULL;

	areTheSame = (char*)malloc(elementCount * elementCount);
	memset(areTheSame, 0, elementCount * elementCount);
	occ = typed_malloc(offset*, elementCount);
	for (int i = 0; i < elementCount; i++) {
		occ[i] = typed_malloc(offset, PREVIEW);
		proxiScore[i] = 0.0;
		areTheSame[i * elementCount + i] = 1;
	}
	offset *occPos[MAX_SCORER_COUNT];

	// initialize heap structure
	ScoredExtent candidate;
	results = typed_malloc(ScoredExtent, count + 1);
	int resultCount = 0;

	// prune the search by only looking at documents that might contain
	// interesting information; "nextOffsetPossible" contains the next
	// index position at which we can find a query element
	nextOffsetPossible = MAX_OFFSET;
	offset elemStart, elemEnd;
	for (int elem = 0; elem < elementCount; elem++) {
		if (elementLists[elem]->getFirstEndBiggerEq(0, &elemStart, &elemEnd)) {
			nextPossibleForElement[elem] = elemEnd;
			if (nextPossibleForElement[elem] < nextOffsetPossible)
				nextOffsetPossible = nextPossibleForElement[elem];
		}
		else
			nextPossibleForElement[elem] = MAX_OFFSET;
	}

	while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end)) {
		candidate.containerFrom = start;
		candidate.containerTo = end;
		candidate.score = 0.0;

		double containerLength = (end - start + 1);
		double K = k1 * ((1 - b) + b * containerLength / averageContainerLength);
		int scorersInCurrentDocument = 0;
		nextOffsetPossible = MAX_OFFSET;

		// compute score for every term in the element list
		for (int i = 0; i < elementCount; i++) {
			ExtentList *elemList = elementLists[i];
			if (nextPossibleForElement[i] > end) {
				// this element cannot be found inside the current document
				if (nextPossibleForElement[i] < nextOffsetPossible)
					nextOffsetPossible = nextPossibleForElement[i];
			}
			else {
				// could be that we find the element inside the current document
				offset position = start;
				if (elemList->getFirstStartBiggerEq(position, &elemStart, &elemEnd)) {
					nextPossibleForElement[i] = elemEnd;
					if (nextPossibleForElement[i] < nextOffsetPossible)
						nextOffsetPossible = nextPossibleForElement[i];
					if (elemEnd <= end) {
						unsigned int termFrequency =
							1 + elemList->getCount(elemStart + 1, end);
						candidate.score +=
							internalWeights[i] * (k1 + 1.0) * termFrequency / (K + termFrequency);
						tf[i] = termFrequency;
						whichScorer[scorersInCurrentDocument++] = i;
					}
				}
				else
					nextPossibleForElement[i] = MAX_OFFSET;
			}
		} // end for (int i = 0; i < elementCount; i++)

		// perform term proximity scoring for all terms found in the current document
		if (scorersInCurrentDocument == 1)
			elementLists[whichScorer[0]]->getFirstStartBiggerEq(start, &candidate.from, &candidate.to);
		else if (scorersInCurrentDocument > 1) {
			for (int i = 0; i < scorersInCurrentDocument; i++) {
				offset dummy[PREVIEW];
				int who = whichScorer[i];
				if (tf[who] >= PREVIEW)
					tf[who] = PREVIEW - 1;
				elementLists[who]->getNextN(start, end, tf[who], occ[who], dummy);
				occ[who][tf[who]] = MAX_OFFSET;
				occPos[who] = occ[who];
			}

			static const int LOOKBACK = 4;
			int prevTerm[LOOKBACK];
			offset prevPos[LOOKBACK];
			for (int i = 0; i < LOOKBACK; i++) {
				prevTerm[i] = 0;
				prevPos[i] = -999999999;
			}
			double maxProxiScore = 0.0;

			while (true) {
				int who, next;
				offset nextPos = MAX_OFFSET;
				for (int i = 0; i < scorersInCurrentDocument; i++) {
					who = whichScorer[i];
					if (occPos[who][0] < nextPos) {
						next = who;
						nextPos = occPos[who][0];
					}
				}
				if (nextPos >= MAX_OFFSET)
					break;
				who = next;
				if (!areTheSame[prevTerm[0] * elementCount + who]) {
					float distance = (occPos[who][0] - prevPos[0]);
					if (distance < 0.999) {
						areTheSame[prevTerm[0] * elementCount + who] = 1;
						areTheSame[who * elementCount + prevTerm[0]] = 1;
					}
					else {
						proxiScore[who] += internalWeights[prevTerm[0]] / pow(distance, 2.0);
						proxiScore[prevTerm[0]] += internalWeights[who] / pow(distance, 2.0);
					}
				}

				// try to find good passage within this document; use QAP-like heuristic
				if (internalWeights[who] > maxProxiScore) {
					maxProxiScore = internalWeights[who];
					candidate.from = candidate.to = occPos[who][0];
				}
				double accScore = internalWeights[who];
				for (int i = 0; i < LOOKBACK; i++) {
					double dist = MAX(occPos[who][0] - prevPos[i], 2 * elementCount + 1);
					if (dist < 1.0)
						continue;
					if (dist > 12)
						break;
					if (prevTerm[i] != who)
						accScore += internalWeights[prevTerm[i]];
					else
						accScore += 0.3 * internalWeights[prevTerm[i]];
					if (accScore - (i + 2) * log(dist) > maxProxiScore) {
						maxProxiScore = accScore - (i + 2) * log(dist);
						candidate.from = prevPos[i];
						candidate.to = occPos[who][0];
					}
				}

				// update history (previous N postings)
				for (int i = LOOKBACK - 1; i > 0; i--) {
					prevTerm[i] = prevTerm[i - 1];
					prevPos[i] = prevPos[i - 1];
				}
				prevTerm[0] = who;
				prevPos[0] = occPos[who][0];

				// increase "current posting" pointer for current term
				occPos[who]++;

			} // end while (true)
		} // end if (scorersInCurrentDocument > 1)

		for (int i = 0; i < scorersInCurrentDocument; i++) {
			int who = whichScorer[i];
			candidate.score += MIN(1.0, internalWeights[who]) *
				(k1 + 1.0) * proxiScore[who] / (K + proxiScore[who]);
			proxiScore[who] = tf[who] = 0;
		}
		// end of term proximity scoring

		if (nextOffsetPossible <= end)
			nextOffsetPossible = end + 1;

		// add current candidate to top-k result set
		if (candidate.score > 0.0)
			addToResultSet(&candidate, &resultCount);

	} // end while (containerList->getFirstEndBiggerEq(nextOffsetPossible, &start, &end))

	if (occ != NULL) {
		for (int i = 0; i < elementCount; i++)
			free(occ[i]);
		free(occ);
		occ = NULL;
		free(areTheSame);
		areTheSame = NULL;
	}

	count = resultCount;
} // end of processCoreQuery()


char * DesktopQuery::sanitize(char *s) {
	int found = 0;
	for (int i = 0; s[i] != 0; i++)
		if (s[i] == '!')
			if (s[i + 1] == '>')
				found++;
	if (found > 0) {
		char *result = (char*)malloc(strlen(s) + found + 2);
		int outPos = 0;
		for (int i = 0; s[i] != 0; i++) {
			if (s[i] == '!')
				if (s[i + 1] == '>')
					result[outPos++] = ' ';
			result[outPos++] = s[i];
		}
		result[outPos] = 0;
		return result;
	}
	else
		return duplicateString(s);
} // end of sanitize(char*)


char * DesktopQuery::getText(offset start, offset end, bool removeNewLines) {
	if (end < start)
		return duplicateString("");
	char params[64];
	sprintf(params, OFFSET_FORMAT " " OFFSET_FORMAT, start, end);
	GetQuery *gq = new GetQuery(index, "get", EMPTY_MODIFIERS, params, visibleExtents, -1);
	gq->parse();
	char miscBuffer[FilteredInputStream::MAX_FILTERED_RANGE_SIZE + 32];
	int len = 0;
	miscBuffer[0] = 0;
	while (gq->getNextLine(&miscBuffer[len])) {
		if (len >= MIN(FilteredInputStream::MAX_FILTERED_RANGE_SIZE/2, 8192))
			break;
		char *p = miscBuffer;
		if (removeNewLines) {
			while ((p = strstr(p, "\n")) != NULL) {
				*p = ' ';
				p = &p[1];
			}
		}
		len += strlen(&miscBuffer[len]);
	}
	miscBuffer[len] = 0;
	return sanitize(miscBuffer);
} // end of getText(offset, offset, bool)


bool DesktopQuery::getNextLine(char *line) {
	int lineLength = 0;
	line[0] = 0;

getNextLine_START:
	
	if (!ok) {
		finished = true;
		return false;
	}
	if ((position >= count) || (position > resultEnd)) {
		finished = true;
		return false;
	}
	assert(results[position].score > 0.0);

	if (position == 0)
		lineLength += sprintf(&line[lineLength], "<count!>%d</count!>\n", count);

	if (position < resultStart)
		position = resultStart;	

	ExtentList *files = visibleExtents->getExtentList();
	offset fileStart, fileEnd;
	bool listStatus =
		files->getLastStartSmallerEq(results[position].containerFrom, &fileStart, &fileEnd);
	delete files;
	if (!listStatus)
		log(LOG_ERROR, LOG_ID, "Inconsistent crap in getNextLine(char*)");

	// print first, standard part of the query result
	lineLength += sprintf(&line[lineLength],
			"<document!>\n"
			"  <rank!>%d</rank!>\n"
			"  <score!>%.4f</score!>\n"
			"  <file_start!>%lld</file_start!>\n"
			"  <file_end!>%lld</file_end!>\n"
			"  <document_start!>%lld</document_start!>\n"
			"  <document_end!>%lld</document_end!>\n"
			"  <passage_start!>%lld</passage_start!>\n"
			"  <passage_end!>%lld</passage_end!>\n",
			position, results[position].score,
			(long long)fileStart,
			(long long)fileEnd,
			(long long)results[position].containerFrom,
			(long long)results[position].containerTo,
			(long long)results[position].from,
			(long long)results[position].to);

	// add page number (if applicable)
	int endPage = 1 +
		pageNumberList->getCount(results[position].containerFrom, results[position].to);
	if (endPage == 1)
		lineLength +=
			sprintf(&line[lineLength], "  <page!>1</page!>\n");
	else {
		int startPage = 1 +
			pageNumberList->getCount(results[position].containerFrom, results[position].from);
		if (startPage == endPage)
			lineLength +=
				sprintf(&line[lineLength], "<page!>%d</page!>\n", startPage);
		else
			lineLength +=
				sprintf(&line[lineLength], "<page!>%d-%d</page!>\n", startPage, endPage);
	} // end else [endPage > 1]

	char *fileName = visibleExtents->getFileNameForOffset(results[position].from);
	if (fileName == NULL) {
		lineLength += sprintf(&line[lineLength],
				"  <filename!>%s</filename!>\n"
				"  <document_type!>%s</document_type!>\n"
				"  <headers!>%s</headers!>\n"
				"  <snippet!>%s</snippet!>\n",
				"(file not found)", "application/unknown", "(text unavailable)", "(text unavailable)");
	}
	else {
		int documentType = visibleExtents->getDocumentTypeForOffset(results[position].from);
		char *docType = FilteredInputStream::documentTypeToString(documentType);
		lineLength += sprintf(&line[lineLength],
				"  <filename!>%s</filename!>\n"
				"  <document_type!>%s</document_type!>\n",
				fileName, docType);
		free(docType);

		// try to extract some file information (owner, group, ...)
		struct stat buf;
		if (stat(fileName, &buf) == 0) {
			int user = buf.st_uid, group = buf.st_gid, modified = buf.st_mtime;
			offset size = buf.st_size;
			lineLength += sprintf(&line[lineLength],
					"  <owner!>%d</owner!>\n"
					"  <group!>%d</group!>\n"
					"  <modified!>%d</modified!>\n"
					"  <filesize!>" OFFSET_FORMAT "</filesize!>\n",
					user, group, modified, size);
		} // end if (stat(fileName, &buf) == 0)

		// get headers from file
		offset headerStart = results[position].containerFrom;
		offset headerEnd = results[position].containerTo;
		if (headerEnd >= headerStart + HEADER_TOKEN_COUNT)
			headerEnd = headerStart + (HEADER_TOKEN_COUNT - 1);
		char *headers = getText(headerStart, headerEnd, false);

		// get snippet from file
		offset snippetStart = results[position].from;
		offset snippetEnd = results[position].to;
		offset toGive = SNIPPET_TOKEN_COUNT - (snippetEnd - snippetStart + 1);
		snippetStart = MAX(results[position].containerFrom, snippetStart - toGive);
		snippetEnd = MIN(results[position].containerTo, snippetEnd + toGive);
		char *snippet = duplicateString("");
		snippet = concatenateStringsAndFree(
				snippet, getText(snippetStart, results[position].from - 1, true));
		snippet = concatenateStringsAndFree(
				snippet, duplicateString("<passage!>"));
		snippet = concatenateStringsAndFree(
				snippet, getText(results[position].from, results[position].to, true));
		snippet = concatenateStringsAndFree(
				snippet, duplicateString("</passage!>"));
		snippet = concatenateStringsAndFree(
				snippet, getText(results[position].to + 1, snippetEnd, true));
		
		lineLength += sprintf(&line[lineLength],
				"  <headers!>\n%s\n  </headers!>\n  <snippet!>\n%s\n  </snippet!>\n", headers, snippet);
		
		free(headers);
		free(snippet);
		free(fileName);
	} // end else [fileName != NULL]

	// close XML container and return line
	lineLength += sprintf(&line[lineLength], "</document!>\n");
	position++;
	return true;
} // end of getNextLine(char*)


bool DesktopQuery::getStatus(int *code, char *description) {
	if (!finished)
		return false;
	if (!ok) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
	}
	else {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
	}
	return true;
} // end of getStatus(int*, char*)


void DesktopQuery::processModifiers(const char **modifiers) {
	RankedQuery::processModifiers(modifiers);
	k1 = getModifierDouble(modifiers, "k1", DEFAULT_K1);
	b = getModifierDouble(modifiers, "b", DEFAULT_B);
	noIDF = getModifierBool(modifiers, "noidf", false);
	resultStart = getModifierInt(modifiers, "start", resultStart);
	resultEnd = getModifierInt(modifiers, "end", resultEnd);
	if (resultStart < 0)
		resultStart = 0;
	if (resultStart > 1990)
		resultStart = 1990;

	if (resultEnd < resultStart + 9)
		resultEnd = resultStart + 9;
	if (resultEnd > resultStart + 19)
		resultEnd = resultStart + 19;
} // end of processModifiers(char**)


