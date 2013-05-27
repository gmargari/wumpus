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
 * This utility can be used to do all sorts of stuff: stemming, DocID extraction,
 * vocabulary extractions, ...
 * Just call it without parameters in order to see what it can do for you.
 *
 * author: Stefan Buettcher
 * created: 2005-09-29
 * changed: 2008-08-15
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <sys/time.h>
#include <vector>
#include <map>
#include <set>
#include <string>
#include "../feedback/language_model.h"
#include "../filters/trec_inputstream.h"
#include "../index/compactindex2.h"
#include "../index/index_iterator.h"
#include "../index/index_merger.h"
#include "../index/multiple_index_iterator.h"
#include "../indexcache/docidcache.h"
#include "../misc/all.h"
#include "../misc/stopwords.h"
#include "../misc/stringtokenizer.h"
#include "../query/bm25query.h"
#include "../stemming/stemmer.h"
#include "../terabyte/terabyte.h"


using namespace std;

#define TOTAL_BUFFER_SIZE (256 * 1024 * 1024)

#define MERGE_BUFFER_SIZE TOTAL_BUFFER_SIZE


/** Prints usage information to the screen. **/
void usage() {
	fprintf(stderr, "Usage:  handyman WORKMODE [PARAMETERS]\n\n");
	fprintf(stderr, "WORKMODE can be any of the following:\n");
	fprintf(stderr, "- BUILD_LM        Builds a LanguageModel (term freqs and term-document freqs)\n");
	fprintf(stderr, "                  from the list of files found in the file given by the first\n");
	fprintf(stderr, "                  parameter. Output file is specified by the second parameter.\n");
	fprintf(stderr, "- BUILD_DOCUMENT_LENGTH_VECTOR  Takes an existing index file (first parameter)\n");
	fprintf(stderr, "                  and produces a file (given by second parameter) that, for\n");
	fprintf(stderr, "                  each document in the input index, contains its start offset\n");
	fprintf(stderr, "                  (64-bit integer) and the length of its vector (64-bit float).\n");
	fprintf(stderr, "                  An optional, third parameter determines how the vectors are\n");
	fprintf(stderr, "                  constructed. Options are: --tf, --idf, --tfidf.\n");
	fprintf(stderr, "- BUILD_INDEX_FROM_ASCII  Takes an input file containing lines of the form\n");
	fprintf(stderr, "                  \"# TERM OCC_1 OCC_2 .. OCC_N\" and creates an on-disk index\n");
	fprintf(stderr, "                  containing the information found in the input file. The \"#\"\n");
	fprintf(stderr, "                  symbol is mandatory and is used as a list delimiter.\n");
	fprintf(stderr, "                  Parameters: INPUT_FILE OUTPUT_FILE.\n");
	fprintf(stderr, "- COMPRESS_LISTS  Takes lists of docids or TF values, one per line, from stdin\n");
	fprintf(stderr, "                  and compresses them, using the given compression method.\n");
	fprintf(stderr, "- CREATE_EMPTY_INDEX  Creates an empty index with the given file name.\n");
	fprintf(stderr, "- EXTRACT_DOCIDS  Extracts all document IDs from the document ID files given\n");
	fprintf(stderr, "                  as parameters. Results are written one ID per line.\n");
	fprintf(stderr, "- EXTRACT_POSTINGS  Takes two parameters, INDEX and TERM. Prints a list of\n");
	fprintf(stderr, "                  all postings for TERM found in INDEX to stdout.\n");
	fprintf(stderr, "- EXTRACT_VOCAB   Extracts all vocabulary terms, along with their respective\n");
	fprintf(stderr, "                  frequencies, from the given index (i.e., \"index.XXX\") files.\n");
	fprintf(stderr, "- FINALIZE_PRUNED_INDEX  Adds document frequency information to a pruned\n");
	fprintf(stderr, "                  index created by our document-centric pruning method. Takes\n");
	fprintf(stderr, "                  a pruned input index, a language model file, and the name\n");
	fprintf(stderr, "                  of the output file -- a pruned index with DF information.\n");
	fprintf(stderr, "- GET_COMPRESSION_STATS prints compression statistics for the given index (arg1),\n");
	fprintf(stderr, "                  using the given compression method (arg2).\n");
	fprintf(stderr, "- GET_DOCUMENT_INDEX transforms a schema-independent index into a document-\n");
	fprintf(stderr, "                  centric one.\n");
	fprintf(stderr, "- GET_FEATURE_VECTOR  Takes a language model input file and an SVMlight target\n");
	fprintf(stderr, "                  value as parameter. Reads a TREC-formatted input file from\n");
	fprintf(stderr, "                  stdin and prints an SVMlight-compatible vector to stdout.\n");
	fprintf(stderr, "                  Optional parameter \"--file_list\" used to read list of files.\n");
	fprintf(stderr, "- GET_INDEX_STATISTICS  Reports some statistical information about the given\n");
	fprintf(stderr, "                  Wumpus index file, such as number of terms, number of postings\n");
	fprintf(stderr, "                  etc.\n");
	fprintf(stderr, "- GET_TERMID_VECTOR  Takes a language model file and a label string as params.\n");
	fprintf(stderr, "                  Reads a TREC-formatted document from stdin and prints a\n");
	fprintf(stderr, "                  sequence of term IDs to stdout. If \"--file_list\" is given,\n");
	fprintf(stderr, "                  reads a list of input files from stdin instead.\n");
	fprintf(stderr, "- MEASURE_DECODING_PERFORMANCE Takes an inverted file and a compression method.\n");
	fprintf(stderr, "                  Reads a sequence of terms from stdin and measures the decoding\n");
	fprintf(stderr, "                  performance of the given method on the given postings lists.\n");
	fprintf(stderr, "- MERGE_INDICES   Takes a list of input index files followed by the file name\n");
	fprintf(stderr, "                  of the output index. Merges the input files into the target.\n");
	fprintf(stderr, "- RECOMPRESS_INDEX  Takes three parameters: input index, output index, and\n");
	fprintf(stderr, "                  compression algorithm to use. Compression algorithm can be:\n");
	fprintf(stderr, "                  GAMMA, DELTA, GOLOMB, RICE, INTERPOLATIVE, VBYTE, SIMPLE_9,\n");
	fprintf(stderr, "                  LLRUN, GUBC[IP].\n");
	fprintf(stderr, "                  An optional fourth parameter, --verify, can be used to force\n");
	fprintf(stderr, "                  the handyman to make sure that data are compressed correctly.\n");
	fprintf(stderr, "- STEMMING        No commands necessary. Reads words from stdin and writes\n");
	fprintf(stderr, "                  their stemmed forms to stdout.\n");
	fprintf(stderr, "- TF_TO_TERM_CONTRIB  Takes a positionless frequency index and replaces all TF\n");
	fprintf(stderr, "                  values by discretized BM25 score contribs.\n");
	fprintf(stderr, "- TERMIDS_TO_TERMSTRINGS  Counterpart to GET_TERMID_VECTOR. Takes a LM file as\n");
	fprintf(stderr, "                  first parameter. Transforms a sequence of term IDs into the\n");
	fprintf(stderr, "                  corresponding sequence of term strings, using the LM.\n");
	exit(1);
} // end of usage()


char *extractArgument(int &argc, char **argv, const char *s) {
	int len = strlen(s);
	char *result = NULL;
	for (int i = 0; i < argc; i++) {
		if (strncmp(argv[i], "--", 2) != 0)
			continue;
		if (strncasecmp(&argv[i][2], s, len) != 0)
			continue;
		if ((argv[i][len + 2] != 0) && (argv[i][len + 2] != '='))
			continue;

		if (argv[i][len + 2] == 0)
			result = duplicateString("");
		else
			result = duplicateString(&argv[i][len + 3]);
		argc--;
		for (int k = i; k < argc; k++)
			argv[k] = argv[k + 1];
		return result;
	}
	return result;
} // end of extractArgument(...)


bool extractArgumentBool(int &argc, char **argv, const char *s, bool defaultValue) {
	bool result = defaultValue;
	char *v = extractArgument(argc, argv, s);
	if (v != NULL) {
		if ((strcasecmp(v, "true") == 0) || (v[0] == 0))
			result = true;
		if (strcasecmp(v, "false") == 0)
			result = false;
		free(v);
	}
	return result;
} // end of extractArgumentBool(...)


int extractArgumentInt(int &argc, char **argv, const char *s, int defaultValue) {
	int result = defaultValue;
	char *v = extractArgument(argc, argv, s);
	if (v != NULL) {
		int status = sscanf(v, "%d", &result);
		free(v);
		if (status != 1)
			result = defaultValue;
	}
	return result;
} // end of extractArgumentInt(


/** Reads words from stdin and writes their stemmed forms to stdout. **/
void stemming() {
	char line[1024];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		Stemmer::stem(line, LANGUAGE_ENGLISH, false);
		printf("%s\n", line);
	}
} // end of stemming()


void extractDocumentIDs(int fileCount, char **files) {
	if (fileCount <= 0)
		usage();
	for (int i = 0; i < fileCount; i++) {
		struct stat buf;
		if (stat(files[i], &buf) != 0) {
			fprintf(stderr, "Unable to find file or directory: %s\n", files[i]);
			continue;
		}
		if (S_ISDIR(buf.st_mode)) {
			struct stat buf2;
			char *fileName = (char*)malloc(strlen(files[i]) + 32);
			strcpy(fileName, files[i]);
			strcat(fileName, "doc_ids");
			if (stat(fileName, &buf2) != 0) {
				fprintf(stderr, "Unable to find file: %s\n", fileName);
				free(fileName);
				continue;
			}
			free(fileName);
		}
		DocIdCache *docIDs = new DocIdCache(files[i], S_ISDIR(buf.st_mode));
		int bucketCount = docIDs->getBucketCount();
		for (int i = 0; i < bucketCount; i++) {
			char *ids = docIDs->getDocumentIDsInBucket(i);
			printf("%s", ids);
			free(ids);
		}
		delete docIDs;
	}
} // end of extractDocumentIDs(int, char**)


void buildLanguageModel(int argc, char **argv) {
	bool stemmed = extractArgumentBool(argc, argv, "stemmed", false);
	stemmed = !extractArgumentBool(argc, argv, "unstemmed", !stemmed);
	int termCount = extractArgumentInt(argc, argv, "count", 1000000);

	if ((argc != 2) || (termCount <= 0)) {
		fprintf(stderr, "Error: Illegal number of parameters (or illegal parameter values).\n");
		fprintf(stderr, "Usage: BUILD_LM INPUT_FILE OUTPUT_FILE [--stemmed|UNSTEMMED] [--count=NNN]\n\n");
		fprintf(stderr, "INPUT_FILE contains a list of files to be parsed. OUTPUT_FILE will contain\n");
		fprintf(stderr, "the textual representation of the language model defined by the contents of\n");
		fprintf(stderr, "the given files. The language model may either be stemmed (Porter) or\n");
		fprintf(stderr, "unstemmed (default: unstemmed). The LM will be restricted to the NNN most\n");
		fprintf(stderr, "frequent terms in the collection (default: 1,000,000).\n\n");
		exit(1);
	}

	FILE *input = fopen(argv[0], "r");
	if (input == NULL) {
		fprintf(stderr, "Error: Unable to open file \"%s\".\n", argv[0]);
		exit(1);
	}
	struct stat buf;
	if (stat(argv[1], &buf) == 0) {
		fprintf(stderr, "Error: Output file (%s) already exists. Cowardly refusing to run.\n", argv[1]);
		exit(1);
	}
	FILE *output = fopen(argv[1], "w");
	if (output == NULL) {
		fprintf(stderr, "Error: Unable to create file \"%s\".\n", argv[1]);
		exit(1);
	}
	fclose(output);

	double lastCheckPoint = 0;

	LanguageModel *lm = new LanguageModel(0, 0, stemmed);
	char fileName[1024];
	while (fscanf(input, "%s", fileName) > 0) {
		fprintf(stderr, "Processing input file: %s\n", fileName);
		FilteredInputStream *is = FilteredInputStream::getInputStream(fileName, NULL);
		InputToken token;
		LanguageModel *documentModel = NULL;
		while (is->getNextToken(&token)) {
			if (token.token[0] == '<') {
				if (strcmp((char*)token.token, "<doc>") == 0) {
					if (documentModel != NULL) {
						documentModel->setAllDocumentFrequencies(1);
						lm->addLanguageModel(documentModel);
						delete documentModel;
					}
					documentModel = new LanguageModel(0, 1, stemmed);
					documentModel->enableStemmingCache();
					continue;
				}
				else if (strcmp((char*)token.token, "</doc>") == 0) {
					if (documentModel != NULL) {
						documentModel->setAllDocumentFrequencies(1);
						lm->addLanguageModel(documentModel);
						delete documentModel;
					}
					documentModel = NULL;
					continue;
				}
			}
			if (documentModel != NULL) {
				if (token.token[0] != '<')
					documentModel->updateTerm((char*)token.token, 1, 0);
				documentModel->corpusSize += 1;
			}

			// every now and then, restrict size of language model and write
			// current contents to disk
			if (lm->corpusSize > lastCheckPoint + 1E7) {
				if (lm->getTermCount() > 3 * termCount)
					lm->restrictToMostFrequent(2 * termCount);
				lm->saveToFile(argv[1]);
				lastCheckPoint = lm->corpusSize;
			}
		} // end while (is->getNextToken(&token))

		if (documentModel != NULL) {
			documentModel->setAllDocumentFrequencies(1);
			lm->addLanguageModel(documentModel);
			delete documentModel;
		}
		delete is;
	} // end while (fscanf(input, "%s", fileName) > 0)

	lm->restrictToMostFrequent(termCount);
	lm->saveToFile(argv[1]);
	delete lm;
	fclose(input);
} // end of buildLanguageModel(int, char**)


void buildDocumentLengthVector(int argc, char **argv) {
	bool useTF = true, useIDF = true, linear = false;

	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < argc; i++) {
			if (strncmp(argv[i], "--", 2) == 0) {
				if (strcasecmp(argv[i], "--tf") == 0)
					useIDF = !(useTF = true);
				else if (strcasecmp(argv[i], "--idf") == 0)
					useTF = !(useIDF = true);
				else if (strcasecmp(argv[i], "--tfidf") == 0)
					useTF = useIDF = true;
				else if (strcasecmp(argv[i], "--linear_tf") == 0)
					linear = true;
				else {
					fprintf(stderr, "Illegal parameter: %s\n", argv[i]);
					exit(1);
				}
				for (int k = i; k < argc - 1; k++)
					argv[k] = argv[k + 1];
				argc--;
				changed = true;
			}
		}
	}

	if (argc != 2) {
		fprintf(stderr, "Illegal number of parameters.\n");
		fprintf(stderr, "Expected: INPUT_FILE OUTPUT_FILE [--tf|--idf|--tfidf] [--linear_tf]\n\n");
		exit(1);
	}

	if (!fileExists(argv[0])) {
		fprintf(stderr, "Input file does not exist: %s\n", argv[0]);
		exit(1);
	}
	if (fileExists(argv[1])) {
		fprintf(stderr, "Output file already exists. Cowardly refusing to run.\n\n");
		exit(1);
	}

	// obtain list of document start offsets from index
	CompactIndex *index = CompactIndex::getIndex(NULL, argv[0], false);
	ExtentList *documents = index->getPostings("<doc>");
	int docCnt = documents->getLength();
	offset *docStarts = new offset[docCnt + 1];
	double *docVectors = new double[docCnt];
	int status = documents->getNextN(0, MAX_OFFSET, docCnt, docStarts, docStarts);
	assert(status == docCnt);
	docStarts[docCnt] = MAX_OFFSET;
	delete documents;
	for (int i = 0; i < docCnt; i++)
		docVectors[i] = 0;
	PostingList docList(docStarts, docCnt, false, true);

	// acquire iterator for input index
	IndexIterator *iterator = CompactIndex::getIterator(argv[0], 4 * 1024 * 1024);
	char currentTerm[256] = { 0 };
	long long postingsProcessed = 0, lastStatusMessage = 0;
	for (char *nextTerm = iterator->getNextTerm(); nextTerm != NULL; nextTerm = iterator->getNextTerm()) {

		if (strcmp(currentTerm, nextTerm) != 0) {
			strcpy(currentTerm, nextTerm);
			ExtentList *list;
			if (iterator->getNextListHeader()->postingCount < 1024) {
				int len;
				offset *p = iterator->getNextListUncompressed(&len, NULL);
				assert((len > 0) && (len < 1024));
				list = new PostingList(p, len, false, true);
			}
			else {
				list = index->getPostings(currentTerm);
				iterator->skipNext();
			}

			// traverse the posting list once, to obtain IDF values
			offset s, e, pos;
			double idfWeight = 1;
			if (useIDF) {
				pos = 0;
				idfWeight = 0;
				while (list->getFirstStartBiggerEq(pos, &s, &e)) {
					idfWeight++;
					if (!docList.getFirstStartBiggerEq(s + 1, &pos, &pos))
						break;
				}
				assert((idfWeight > 0) && (idfWeight <= docCnt));
				idfWeight = log(docCnt / idfWeight);
			} // end if (useIDF)

			// traverse the list a second time, to update document vectors
			pos = 0;
			while (list->getFirstStartBiggerEq(pos, &s, &e)) {
				offset ds;
				if (!docList.getLastStartSmallerEq(s, &ds, &ds))
					pos = pos + 1;
				else {
					int curDoc = docList.getInternalPosition();
					assert((curDoc >= 0) && (curDoc < docCnt));
					double tfWeight = 1;
					if (useTF) {
						int tf = list->getCount(ds, docStarts[curDoc + 1] - 1);
						if (linear)
							tfWeight = tf;
						else
							tfWeight = log(tf) / log(2) + 1;
					}
					docVectors[curDoc] += pow(tfWeight * idfWeight, 2);
					pos = docStarts[curDoc + 1];
				}
			} // end while (list->getFirstStartBiggerEq(pos, &s, &e))

			postingsProcessed += list->getLength();
			delete list;
		}
		else
			iterator->skipNext();

		if (postingsProcessed > lastStatusMessage + 10000000) {
			fprintf(stderr, "%lld postings processed.\n", postingsProcessed);
			lastStatusMessage = postingsProcessed;
		}
	} // end for (char *nextTerm = iterator->getNextTerm(); nextTerm != NULL; nextTerm = iterator->getNextTerm())
	delete index;
	delete iterator;

	// create output file and write results to disk
	FILE *f = fopen(argv[1], "w");
	assert(f != NULL);
	for (int i = 0; i < docCnt; i++) {
		assert(docVectors[i] > 0);
		fwrite(&docStarts[i], sizeof(offset), 1, f);
		docVectors[i] = sqrt(docVectors[i]);
		fwrite(&docVectors[i], sizeof(double), 1, f);
	}
	fclose(f);
} // end of buildDocumentLengthVector(int, char**)


void buildIndexFromASCII(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Illegal number of parameters. Expected: INPUT_FILE OUTPUT_FILE.\n");
		exit(1);
	}
	FILE *f = fopen(argv[0], "r");
	if (f == NULL) {
		fprintf(stderr, "Error: Unable to open input file \"%s\".\n", argv[0]);
		exit(1);
	}

	CompactIndex *target = CompactIndex::getIndex(NULL, argv[1], true);
	offset postings[MAX_SEGMENT_SIZE];
	int pCnt = 0;
	char token[256];
	char term[256];
	term[0] = 0;

	while (fscanf(f, "%s", token) == 1) {
		if (token[0] == '#') {
			// list delimiter encountered: read new term
			if (pCnt > 0)
				target->addPostings(term, postings, pCnt);
			int status = fscanf(f, "%s", term);
			if (status != 1) {
				fprintf(stderr, "Error: Illegal input.\n");
				exit(1);
			}
			pCnt = 0;
		}
		else if (term[0] != 0) {
			// process posting
			long long ll;
			assert(sscanf(token, "%lld", &ll) == 1);
			postings[pCnt++] = ll;
			if (pCnt > 1)
				if (postings[pCnt - 2] >= postings[pCnt - 1]) {
					fprintf(stderr, "Error: Postings for term \"%s\" are not sorted.\n", term);
					exit(1);
				}
			if (pCnt >= MAX_SEGMENT_SIZE) {
				target->addPostings(term, postings, MIN_SEGMENT_SIZE);
				memmove(postings, &postings[MIN_SEGMENT_SIZE], (MAX_SEGMENT_SIZE - MIN_SEGMENT_SIZE) * sizeof(offset));
				pCnt -= MIN_SEGMENT_SIZE;
			}
		}
	} // end while (fscanf(line, "%s", token) == 1)
	if (pCnt > 0)
		target->addPostings(term, postings, pCnt);

	fclose(f);
	delete target;
} // end of buildIndexFromASCII(int, char**)


void extractVocabularyTerms(int fileCount, char **files) {
	if (fileCount <= 0) {
		fprintf(stderr, "Usage:  EXTRACT_VOCAB INDEX_FILE_1 .. INDEX_FILE_N\n");
		exit(1);
	}
	IndexIterator **iterators = typed_malloc(IndexIterator*, fileCount);
	for (int i = 0; i < fileCount; i++)
		iterators[i] = CompactIndex::getIterator(files[i], TOTAL_BUFFER_SIZE / fileCount);
	IndexIterator *iterator = new MultipleIndexIterator(iterators, fileCount);

	char currentTerm[MAX_TOKEN_LENGTH * 2];
	currentTerm[0] = 0;
	offset occurrences = 0;
	while (iterator->hasNext()) {
		char *term = iterator->getNextTerm();
		if (strcmp(term, currentTerm) != 0) {
			if (currentTerm[0] != 0)
				printf("%s %lld\n", currentTerm, (long long)occurrences);
			strcpy(currentTerm, term);
			occurrences = 0;
		}
		int length, size;
		byte *buffer = iterator->getNextListCompressed(&length, &size, NULL);
		occurrences += length;
		free(buffer);
	} // end while (iterator->hasNext())
	if (currentTerm[0] != 0)
		printf("%s %lld\n", currentTerm, (long long)occurrences);

	delete iterator;
} // end of extractVocabularyTerms(int, char**)


/**
 * Takes an array of document start positions ("docStarts"), a current index in
 * that array ("docno"), and the position of a term occurrence ("termPosition").
 * Returns the index of the document containing the given term occurrence, or
 * -1 if there is no such document.
 **/
static int getDocno(offset *docStarts, int docno, int documentCount, offset termPosition) {
	if (docno >= documentCount - 1)
		return -1;
	if ((termPosition > docStarts[documentCount - 1]) || (termPosition < docStarts[0]))
		return -1;
	for (int i = 1; i <= 3; i++)
		if (docStarts[docno + i] > termPosition)
			return docno + i - 1;
	int lower = MAX(0, docno);
	int delta = 2;
	while (docStarts[lower + delta] < termPosition) {
		delta += delta;
		if (lower + delta >= documentCount) {
			delta = documentCount - 1 - lower;
			break;
		}
	}
	int upper = lower + delta;
	lower = upper - (delta >> 1);
	while (upper > lower) {
		int middle = ((upper + lower + 1) >> 1);
		if (docStarts[middle] > termPosition)
			upper = middle - 1;
		else
			lower = middle;
	}
	return lower;
} // end of getDocno(offset*, int, int, offset)


void extractPostings(int argc, char **argv) {
	if ((argc < 2) || (argc > 3))
		usage();
	int mode = 0;
	if (argc == 3) {
		if (strcasecmp(argv[2], "--docpositions") == 0)
			mode = 1;
		else if (strcasecmp(argv[2], "--docnos") == 0)
			mode = 2;
		else if (strcasecmp(argv[2], "--tf_values") == 0)
			mode = 3;
		else
			usage();
	}

	vector<string> terms;
	if (strcmp(argv[1], "-") != 0)
		terms.push_back(argv[1]);
	else {
		char term[1024];
		while (scanf("%s", term) == 1)
			terms.push_back(term);
		sort(terms.begin(), terms.end());
	}

	CompactIndex *index = CompactIndex::getIndex(NULL, argv[0], false);

	if (mode == 0) {
		for (unsigned int i = 0; i < terms.size(); i++) {
			ExtentList *list = index->getPostings((char*)terms[i].c_str());
			offset posting = -1;
			while (list->getFirstStartBiggerEq(posting + 1, &posting, &posting)) {
				long long p = posting;
				printf("%lld ", p);
			}
			printf("\n");
			delete list;
		}
	}
	else {
		ExtentList *docList = index->getPostings("<doc>");
		int documentCount = docList->getLength() + 1;
		assert(documentCount > 1);
		offset *docStarts = new offset[documentCount + 1];
		offset position = -1;
		int cnt = 0;
		while (docList->getFirstStartBiggerEq(position + 1, &docStarts[cnt], &docStarts[cnt]))
			position = docStarts[cnt++];
		docStarts[cnt++] = MAX_OFFSET;
		assert(cnt == documentCount);
		delete docList;

		for (unsigned int i = 0; i < terms.size(); i++) {
			ExtentList *list = index->getPostings((char*)terms[i].c_str());
			offset currentPosting = 0;
			if (!list->getFirstStartBiggerEq(0, &currentPosting, &currentPosting)) {
				delete list;
				continue;
			}
			offset prevPrinted = 0;
			offset prevPosting;
			int tf, docno = 0;
			while ((docno = getDocno(docStarts, docno, documentCount, currentPosting)) >= 0) {
				switch (mode) {
					case 1:  // docpositions
						prevPosting = docStarts[docno];
						while (list->getFirstStartBiggerEq(prevPosting + 1, &currentPosting, &currentPosting)) {
							if (currentPosting >= docStarts[docno + 1])
								break;
							long long delta = currentPosting - prevPosting;
							prevPrinted += delta;
							printf("%lld ", (long long)prevPrinted);
							prevPosting = currentPosting;
						}
						break;
					case 2:  // docnos
						printf("%d ", docno);
						break;
					case 3:  // tf_values
						tf = list->getCount(docStarts[docno], docStarts[docno + 1]);
						printf("%d ", tf);
						break;
					default:
						assert("This should never happen!" == NULL);
				}
				if (!list->getFirstStartBiggerEq(docStarts[docno + 1], &currentPosting, &currentPosting))
					break;
			}
			printf("\n");
			delete list;
		}

		delete[] docStarts;
	}

	delete index;
} // end of extractPostings(int, char**)


static void updateCompressionStats(
		offset *postings, int pCnt, Compressor c, long long *cnt, long long *size) {
	static const int BLOCK_SIZE = 16384;
	int byteSize;
	while (pCnt >= 2 * BLOCK_SIZE) {
		free(c(postings, BLOCK_SIZE, &byteSize));
		*cnt += BLOCK_SIZE;
		*size += byteSize;
		pCnt -= BLOCK_SIZE;
		postings += BLOCK_SIZE;
	}
	free(c(postings, pCnt, &byteSize));
	*cnt += pCnt;
	*size += byteSize;
} // end of updateCompressionStats(...)


static void processPostings(offset *postings, int pCnt, offset *docStarts, int dCnt,
		long long *docidCnt, long long *docidSize, long long *tfCnt, long long *tfSize,
		long long *posCnt, long long *posSize, long long *siCnt, long long *siSize, Compressor c) {
	offset *docid = new offset[pCnt];
	offset *tf = new offset[pCnt];
	offset *pos = new offset[pCnt];
	int dc = 0, pc = 0;

	if (dCnt > 0) {
		offset prevDocPos = 0;
		int docno = -1;
		for (int i = 0; i < pCnt; i++) {
			if (postings[i] < docStarts[0])
				continue;
			if (postings[i] >= docStarts[docno + 1]) {
				docno = getDocno(docStarts, docno, dCnt, postings[i]);
				docid[dc] = docno;
				tf[dc] = (dc == 0 ? 0 : tf[dc - 1]);
				prevDocPos = docStarts[docno] - 1;
				dc++;
			}
			tf[dc - 1]++;
			pos[pc++] = postings[i] - prevDocPos;
			if (pc > 1)
				pos[pc - 1] += pos[pc - 2];
			prevDocPos = postings[i];
		}
#if 0
		assert(pc == pCnt);
#endif
	} // end if (dCnt > 0)

	updateCompressionStats(postings, pCnt, c, siCnt, siSize);
	updateCompressionStats(docid, dc, c, docidCnt, docidSize);
	updateCompressionStats(tf, dc, c, tfCnt, tfSize);
	updateCompressionStats(pos, pc, c, posCnt, posSize);

	delete[] docid;
	delete[] tf;
	delete[] pos;
} // end of processPostings(...)


static void getCompressionStats(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage:  GET_COMPRESSION_STATS INDEX_FILE_NAME COMPRESSION_METHOD\n\n");
		fprintf(stderr, "Computes compression effectiveness values for docids, tf values, etc.\n");
		fprintf(stderr, "and prints them to stdout. Input index must be schema-independent with\n");
		fprintf(stderr, "<doc> tags. Computation may take a while. Be patient.\n");
		exit(1);
	}

	long long docidCnt = 0, docidSize = 0;
	long long tfCnt = 0, tfSize = 0;
	long long posCnt = 0, posSize = 0;
	long long siCnt = 0, siSize = 0;

	// open input file for reading
	CompactIndex *index = CompactIndex::getIndex(NULL, argv[0], false);
	IndexIterator *input = CompactIndex::getIterator(argv[0], 1024 * 1024);
	Compressor c = compressorForID[getCompressorForName(argv[1])];

	// extract document delimiters
	ExtentList *docList = index->getPostings("<doc>");
	int documentCount = docList->getLength() + 1;
	offset *docStarts = new offset[documentCount + 1];
	offset position = 0;
	int cnt = 0;
	while (docList->getFirstStartBiggerEq(position + 1, &docStarts[cnt], &docStarts[cnt]))
		position = docStarts[cnt++];
	docStarts[cnt++] = MAX_OFFSET;
#if 1
	assert(cnt == documentCount);
#else
	documentCount = cnt;
#endif
	delete docList;
	if (documentCount == 1) {
		fprintf(stderr, "Warning: No \"<doc>\" tags found in index. Computing schema-independent statistics only.\n");
	}

	char currentTerm[MAX_TOKEN_LENGTH + 1] = { 0 };
	static const int MAX_POSTINGS = 1000000;
	offset *postings = new offset[MAX_POSTINGS];
	int pCnt = 0;
	while (input->hasNext()) {
		if (strcmp(currentTerm, input->getNextTerm()) != 0) {
			processPostings(postings, pCnt, docStarts, documentCount,
					&docidCnt, &docidSize, &tfCnt, &tfSize, &posCnt, &posSize, &siCnt, &siSize, c);
			pCnt = 0;
			strcpy(currentTerm, input->getNextTerm());
		}
		if (pCnt + input->getNextListHeader()->postingCount > MAX_POSTINGS) {
			processPostings(postings, pCnt, docStarts, documentCount,
					&docidCnt, &docidSize, &tfCnt, &tfSize, &posCnt, &posSize, &siCnt, &siSize, c);
			pCnt = 0;
		}
		int length;
		input->getNextListUncompressed(&length, &postings[pCnt]);
		pCnt += length;
	}
	processPostings(postings, pCnt, docStarts, documentCount,
			&docidCnt, &docidSize, &tfCnt, &tfSize, &posCnt, &posSize, &siCnt, &siSize, c);
	delete index;
	delete input;

//	assert(posCnt == siCnt);
	assert(docidCnt == tfCnt);

	printf("Number of docids:       %12lld\n", docidCnt);
	printf("  Bits per element:     %12.3lf\n", docidSize * 8.0 / docidCnt);
	printf("Number of TF values:    %12lld\n", tfCnt);
	printf("  Bits per element:     %12.3lf\n", tfSize * 8.0 / tfCnt);
	printf("Number of docpositions: %12lld\n", posCnt);
	printf("  Bits per element:     %12.3lf\n", posSize * 8.0 / posCnt);
	printf("Number of SI positions: %12lld\n", siCnt);
	printf("  Bits per element:     %12.3lf\n", siSize * 8.0 / siCnt);
} // end of getCompressionStatistics(int, char**)


static void getDocumentIndex(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  GET_DOCUMENT_INDEX INPUT_INDEX OUTPUT_INDEX [--docids|--tf_values|--docpos]\n\n");
		fprintf(stderr, "Takes a given schema-independent index file and outputs a document-centric\n");
		fprintf(stderr, "index. The output index will either contain lists of docids or list of TF\n");
		fprintf(stderr, "values (transformed into ascending sequences in the latter case).\n\n");
		exit(1);
	}
	
	// open input file for reading
	CompactIndex *index = CompactIndex::getIndex(NULL, argv[0], false);
	IndexIterator *input = CompactIndex::getIterator(argv[0], 1024 * 1024);

	bool outputDocids = (strcasecmp(argv[2], "--docids") == 0);
	bool outputTF = (strcasecmp(argv[2], "--tf_values") == 0);
	bool outputDocpos = (strcasecmp(argv[2], "--docpos") == 0);
	if (strcasecmp(argv[2], "--docids+tf_values") == 0)
		outputDocids = outputTF = true;
	if (!(outputDocids || outputTF || outputDocpos))
		getDocumentIndex(0, NULL);

	// extract document delimiters
	ExtentList *docList = index->getPostings("<doc>");
	int documentCount = docList->getLength() + 1;
	offset *docStarts = new offset[documentCount + 1];
	offset position = -1;
	int cnt = 0;
	while (docList->getFirstStartBiggerEq(position + 1, &docStarts[cnt], &docStarts[cnt]))
		position = docStarts[cnt++];
	docStarts[cnt++] = MAX_OFFSET;
	assert(cnt == documentCount);
	delete docList;
	if (documentCount <= 1) {
		fprintf(stderr, "Error: No \"<doc>\" tags found in input index.\n");
		exit(1);
	}

	// open output file for writing
	CompactIndex *outputIndex = CompactIndex::getIndex(NULL, argv[1], true);
	offset *docids = new offset[documentCount + 3 * MAX_SEGMENT_SIZE];
	offset *tfValues = new offset[documentCount + 3 * MAX_SEGMENT_SIZE];
	int documentsSeen = 0;

	int prevDocno = -1;
	char currentTerm[MAX_TOKEN_LENGTH + 1] = { 0 };
	offset previousInput = 0, previousOutput = 0;
	while (input->hasNext()) {
		int pCnt, docno = 0;
		strcpy(currentTerm, input->getNextTerm());
		offset *postings = input->getNextListUncompressed(&pCnt, NULL);
		if (outputDocpos) {
			prevDocno = -1;
			for (int i = 0; i < pCnt; i++) {
				docno = getDocno(docStarts, docno, documentCount, postings[i]);
				if (docno < 0)
					continue;
				offset delta;
				if (docno == prevDocno) {
					// Still within the same document. Delta relative to previous posting.
					delta = postings[i] - previousInput;
				} else {
					// New document. Compute the delta relative to the beginning of the doc.
					delta = postings[i] - docStarts[docno] + 1;
				}
				assert(delta > 0);
				previousInput = postings[i];
				docids[documentsSeen++] = previousOutput + delta;
				previousOutput += delta;
				prevDocno = docno;
			}
			while (documentsSeen > TARGET_SEGMENT_SIZE + MIN_SEGMENT_SIZE) {
				outputIndex->addPostings(currentTerm, docids, TARGET_SEGMENT_SIZE);
				memmove(docids, &docids[TARGET_SEGMENT_SIZE],
						(documentsSeen - TARGET_SEGMENT_SIZE) * sizeof(offset));
				documentsSeen -= TARGET_SEGMENT_SIZE;
			}
		} // end if (outputDocpos)
		else {
			for (int i = 0; i < pCnt; i++) {
				docno = getDocno(docStarts, docno, documentCount, postings[i]);
				if (docno < 0)
					continue;
				if (docno != prevDocno) {
					tfValues[documentsSeen] = 0;
					docids[documentsSeen++] = docno;
				}
				if (documentsSeen > 0)
					tfValues[documentsSeen - 1]++;
				prevDocno = docno;
			}
		} // end else [!outputDocpos]
		free(postings);

		if ((!input->hasNext()) || (strcmp(currentTerm, input->getNextTerm()) != 0)) {
			if (documentsSeen > 0) {
				if (outputDocids && outputTF) {
					for (int i = 0; i < documentsSeen; i++)
						docids[i] = (docids[i] << DOC_LEVEL_SHIFT) + encodeDocLevelTF(tfValues[i]);
					outputIndex->addPostings(currentTerm, docids, documentsSeen);
				}
				else if (outputTF) {
					for (int i = 1; i < documentsSeen; i++)
						tfValues[i] += tfValues[i - 1];
					outputIndex->addPostings(currentTerm, tfValues, documentsSeen);
				}
				else
					outputIndex->addPostings(currentTerm, docids, documentsSeen);
			}
			prevDocno = -1;
			documentsSeen = 0;
			previousInput = 0;
			previousOutput = 0;
			if (input->hasNext())
				strcpy(currentTerm, input->getNextTerm());
		}
	} // end while (input->hasNext())

	delete index;
	delete input;
	delete outputIndex;
} // end of getDocumentIndex(int, char**)


static void createEmptyIndex(int argc, char **argv) {
	if (argc != 1) {
		fprintf(stderr, "Error: You have to specify exactly one output file.\n");
		exit(1);
	}
	char *fileName = argv[0];
	struct stat buf;
	if (stat(fileName, &buf) == 0) {
		fprintf(stderr, "Error: Output file already exists.\n");
		exit(1);
	}
	CompactIndex *index = CompactIndex::getIndex(NULL, fileName, true);
	delete index;
} // end of createEmptyIndex(int, char**)


static void compressList(offset *list, int count, int method, bool print,
		double *bitsPerPosting, double *secondsPerPosting) {
	bool increasing = true;
	bool positive = true;
	for (int i = 0; i < count; i++)
		if (list[i] <= 0) {
			positive = false;
			break;
		}
	for (int i = 1; i < count; i++)
		if (list[i] <= list[i - 1]) {
			increasing = false;
			break;
		}
	assert(increasing || positive);
	if (!increasing)
		for (int i = 1; i < count; i++)
			list[i] += list[i - 1];
	int byteLength;

	// give special treatment to HUFFMAN_DIRECT, which expects a bunch of positive
	// integers instead of an increasing sequence
	if (compressorForID[method] == compressHuffmanDirect)
		for (int i = count - 1; i > 0; i--)
			list[i] -= list[i - 1];

	byte *compressed = compressorForID[method](list, count, &byteLength);
	if (bitsPerPosting != NULL)
		*bitsPerPosting = byteLength * 8.0 / count;
	if (print)
		fwrite(compressed, 1, byteLength, stdout);

	int listLength;
	if (secondsPerPosting != NULL) {
		*secondsPerPosting = 0;
		if (decompressorForID[method] != 0) {
			sched_yield();
			int prev, cur = 0;
			double end, start = getCurrentTime();
			do {
				prev = cur;
				while (cur < prev + 200000) {
					offset *dummy = decompressList(compressed, byteLength, &listLength, NULL);
					assert(listLength == count);
					free(dummy);
					cur += listLength;
				}
			} while ((end = getCurrentTime()) < start + 0.5);
			*secondsPerPosting = (end - start) / cur;
		}
	}
#if 1
	if (decompressorForID[method] != 0) {
		offset *uncompressed = decompressList(compressed, byteLength, &listLength, NULL);
		for (int i = 0; i < count; i++) {
			if (uncompressed[i] != list[i]) {
				fprintf(stderr, "Mismatch at position %d. Expected: %lld. Seen: %lld.\n",
				        i, (long long)(list[i]), (long long)(uncompressed[i]));
			}
			assert(uncompressed[i] == list[i]);
		}
		assert(listLength == count);
		if (uncompressed != NULL)
			free(uncompressed);
	}
#endif

	free(compressed);
} // end of compressList(offset*, int, int, bool, double*, double*)


static void compressLists(int argc, char **argv) {
	if ((argc < 1) || (argc > 2)) {
		fprintf(stderr, "Supported compression methods are: vbyte, gamma, huffman, interpol, ...\n");
		exit(1);
	}
	int compressionMethod = getCompressorForName(argv[0]);
	bool print = false;
	if (argc == 2) {
		if (strcasecmp(argv[1], "--print") == 0)
			print = true;
		else
			compressLists(0, NULL);
	}

	static const int BLOCK_SIZE = 10000;
	char buffer[65536];
	int bufferPos = 0;

	int c;
	while ((c = fgetc(stdin)) != EOF) {
		ungetc(c, stdin);
		offset postings[2 * BLOCK_SIZE];
		int totalPostings = 0;
		int postingCount = 0;
		double totalSize = 0;
		double bitsPerEntry;

		struct timeval start, end;
		gettimeofday(&start, NULL);

		while ((c = fgetc(stdin)) != '\n') {
			if (c == EOF)
				break;
			if (c > ' ') {
				ungetc(c, stdin);
				long long value;
				assert(fscanf(stdin, "%lld", &value) == 1);
				postings[postingCount++] = value;
				if (postingCount >= 2 * BLOCK_SIZE) {
					compressList(postings, BLOCK_SIZE, compressionMethod, print, &bitsPerEntry, NULL);
					totalSize += bitsPerEntry * BLOCK_SIZE;
					postingCount -= BLOCK_SIZE;
					memmove(postings, &postings[BLOCK_SIZE], postingCount * sizeof(offset));
					totalPostings += BLOCK_SIZE;
				}
			}
		}
		double nsPerPosting = 0;
		if (postingCount > 0) {
			compressList(postings, postingCount, compressionMethod, print, &bitsPerEntry, NULL);
			nsPerPosting *= 1E9;
			totalSize += bitsPerEntry * postingCount;
			totalPostings += postingCount;
		}
		if ((totalPostings > 0) && (!print)) {
			printf("%9d   %6.3lf   %6.3lf\n", totalPostings, totalSize / totalPostings, nsPerPosting);
			fflush(stdout);
		}
	}

} // end of compressList(int, char**)


static void finalizePrunedIndex(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Illegal number of parameters. Specify input and output file(s).\n");
		fprintf(stderr, "Usage:  handyman FINALIZE_PRUNED_INDEX INPUT_INDEX LM_FILE OUTPUT_INDEX\n");
		exit(1);
	}

	static const int DEFAULT_ALLOCATION = 1024 * 1024;
	int allocated = DEFAULT_ALLOCATION;
	offset *postings = typed_malloc(offset, allocated);
	int postingCount = 0;

	LanguageModel *lm = new LanguageModel(argv[1]);
	CompactIndex *targetIndex = CompactIndex::getIndex(NULL, argv[2], true);

	// scan the index and collect all posting lists
	IndexIterator *iter = CompactIndex::getIterator(argv[0], 1024 * 1024);
	char *currentTerm = duplicateString("");
	for (char *nextTerm = iter->getNextTerm(); nextTerm != NULL; nextTerm = iter->getNextTerm()) {
		if (strcmp(nextTerm, currentTerm) != 0) {
			// if we are done reading postings for the current term, append DF and
			// continue with next list
			offset tf, df;
			if (strncmp(currentTerm, "<!>", 3) == 0)
				lm->getTermInfo(&currentTerm[3], &tf, &df);
			else
				lm->getTermInfo(currentTerm, &tf, &df);
			if ((df > 0) && (postingCount > 0)) {
				assert(postings[postingCount - 1] < DOCUMENT_COUNT_OFFSET);
				postings[postingCount++] = DOCUMENT_COUNT_OFFSET + df;
				targetIndex->addPostings(currentTerm, postings, postingCount);
			}
			// deallocate some memory if possible
			if (allocated > DEFAULT_ALLOCATION) {
				allocated = DEFAULT_ALLOCATION;
				postings = typed_realloc(offset, postings, allocated);
			}
			postingCount = 0;
			free(currentTerm);
			currentTerm = duplicateString(nextTerm);
		}

		// read more postings for the current term from the input index; resize
		// "postings" array if necessary
		PostingListSegmentHeader *plsh = iter->getNextListHeader();
		if (postingCount + plsh->postingCount >= allocated) {
			allocated += 4 * plsh->postingCount;
			postings = typed_realloc(offset, postings, allocated);
		}
		int length;
		iter->getNextListUncompressed(&length, &postings[postingCount]);
		assert(postingCount + length < allocated);
		postingCount += length;
	}
	delete iter;

	// add remaining postings to the target index
	if (postingCount > 0) {
		offset tf, df;
		if (strncmp(currentTerm, "<!>", 3) == 0)
			lm->getTermInfo(&currentTerm[3], &tf, &df);
		else
			lm->getTermInfo(currentTerm, &tf, &df);
		if (df > 0) {
			assert(postings[postingCount - 1] < DOCUMENT_COUNT_OFFSET);
			postings[postingCount++] = DOCUMENT_COUNT_OFFSET + df;
			targetIndex->addPostings(currentTerm, postings, postingCount);
		}
	}
	free(currentTerm);
	free(postings);
	delete lm;
	delete targetIndex;
} // end of finalizePrunedIndex(int, char**)


static void getFeatureVector(int argc, char **argv) {
	bool fileList = false;
	bool okapiScores = false;
	bool lmdScores = false;
	bool rawTfScores = false;
	set<string> *allowedFeatures = NULL;
	map<string,int> *featureID = NULL;

	// process modifiers
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < argc; i++) {
			if (strncmp(argv[i], "--", 2) == 0) {
				if (strcasecmp(argv[i], "--file_list") == 0)
					fileList = true;
				else if (strcasecmp(argv[i], "--okapi_scores") == 0)
					okapiScores = true;
				else if (strcasecmp(argv[i], "--lmd_scores") == 0)
					lmdScores = true;
				else if (strcasecmp(argv[i], "--tfidf_scores") == 0)
					okapiScores = false;
				else if (strcasecmp(argv[i], "--raw_tf_scores") == 0)
					rawTfScores = false;
				else if (strncasecmp(argv[i], "--allowed_features=", 19) == 0) {
					allowedFeatures = new set<string>();
					featureID = new map<string,int>();
					FILE *f = fopen(&argv[i][19], "r");
					assert(f != NULL);
					char line[256];
					while (fgets(line, sizeof(line), f) != NULL) {
						char *feature = duplicateAndTrim(line);
						if (strlen(feature) > 0) {
							allowedFeatures->insert(feature);
							(*featureID)[feature] = featureID->size();
						}
						free(feature);
					}
					fclose(f);
				}
				else {
					fprintf(stderr, "Illegal parameter: %s\n", argv[i]);
					exit(1);
				}
				for (int k = i; k < argc - 1; k++)
					argv[k] = argv[k + 1];
				argc--;
				changed = true;
			}
		}
	}

	if (argc != 2) {
		fprintf(stderr, "Illegal number of parameters.\n\n");
		fprintf(stderr, "Usage:  GET_FEATURE_VECTOR LM_FILE TARGET_VALUE [--file_list] [--okapi_scores] < TREC_TEXT > SVM_LIGHT_VECTOR\n\n");
		exit(1);
	}

	LanguageModel lm(argv[0]);
	lm.enableStemmingCache();
	assert(lm.corpusSize > 0);
	assert(lm.documentCount > 0);
	double avgDocLen = lm.corpusSize / lm.documentCount;
	
	do {
		char fileName[1024];
		TRECInputStream *inputStream;
		if (fileList) {
			if (scanf("%s", fileName) != 1)
				break;
			inputStream = new TRECInputStream(fileName);
		}
		else
			inputStream = new TRECInputStream(fileno(stdin));

		static const double k1 = 1.2;
		static const double b = 0.75;
		static const double mu = 2000;

		// build document token vector
		InputToken token;
		map<int,int> tf;
		double docLen = 0;
		vector<int> docVector;
		while (inputStream->getNextToken(&token)) {
			int id = lm.getTermID((char*)token.token);
			if (id < 0)
				continue;
			if (tf.find(id) == tf.end())
				tf[id] = 1;
			else
				tf[id]++;
			docLen++;
			if (lm.getDocumentProbability(id) > 0.1)
				docVector.push_back(id);
		}
		delete inputStream;

		// translate TF values into TF-IDF scores
		map<int,double> score;	
		double totalScore = 0;
		for (map<int,int>::iterator iter = tf.begin(); iter != tf.end(); ++iter) {
			if (okapiScores) {
				double f = iter->second;
				double p = lm.getDocumentProbability(iter->first);
				score[iter->first] = -log(p) * f * (k1 + 1) / (f + k1 * (1 - b + b * docLen / avgDocLen));
			}
			else if (lmdScores) {
				double f = iter->second;
				double globalProb = lm.getTermProbability(iter->first);
				score[iter->first] = log((f + mu * globalProb) / (docLen + mu));
			}
			else if (rawTfScores)
				score[iter->first] = iter->second;
			else {
				double p = lm.getDocumentProbability(iter->first);
				score[iter->first] = -log(p) * iter->second;
			}
			totalScore += pow(score[iter->first], 2);
		}

		// if we are given a list of allowed features, apply filter and map original
		// feature IDs to new IDs; assigning new IDs is necessary because we may want
		// to include bigrams etc.
		if (allowedFeatures != NULL) {
			map<int,double> newScore;
			for (map<int,double>::iterator iter = score.begin(); iter != score.end(); ++iter) {
				char dummy[16];
				sprintf(dummy, "%d", iter->first + 1);
				if (featureID->find(dummy) == featureID->end())
					continue;
				newScore[(*featureID)[dummy]] = iter->second;
			}

			// compute within-document TF values for bigrams and trigrams
			map<int,double> ngramWeight;
			map<int,int> newTF;
			for (unsigned int i = 0; i < docVector.size(); i++) {
				string feature = "";
				double weightSum = 0;
				for (unsigned int k = 0; (k < 3) && (i + k < docVector.size()); k++) {
					char dummy[16];
					sprintf(dummy, "%s%d", k == 0 ? "" : " ", docVector[i + k] + 1);
					feature += dummy;
					weightSum += -log(lm.getDocumentProbability(docVector[i + k]));
					if ((featureID->find(feature) == featureID->end()) || (k == 0))
						continue;
					int id = (*featureID)[feature];
					ngramWeight[id] = weightSum;
					if (newTF.find(id) == newTF.end())
						newTF[id] = 1;
					else
						newTF[id]++;
				}
			} // end for (int i = 0; i < docVector.size(); i++)

			// compute scores for bigrams and trigrams
			for (map<int,int>::iterator iter = newTF.begin(); iter != newTF.end(); ++iter) {
				if (okapiScores) {
					double f = iter->second;
					newScore[iter->first] =
						ngramWeight[iter->first] * f * (k1 + 1) / (f + k1 * (1 - b + b * docLen / avgDocLen));
				}
				else if (rawTfScores)
					newScore[iter->first] = iter->second;
				else if (lmdScores)
					assert(false);
				else
					newScore[iter->first] = iter->second;
			}

			score = newScore;
		} // end if (allowedFeatures != NULL)

		// print feature vector to stdout, including the leading target value
		printf("%s", argv[1]);
		for (map<int,double>::iterator iter = score.begin(); iter != score.end(); ++iter) {
			if ((okapiScores) || (lmdScores) || (rawTfScores))
				printf(" %d:%g", iter->first + 1, iter->second);
			else
				printf(" %d:%g", iter->first + 1, iter->second / sqrt(totalScore));
		}

		if (fileList)
			printf(" # %s", fileName);
		printf("\n");
	} while (fileList);

} // end of getFeatureVector(int, char**)


static void getTermIdVector(int argc, char **argv) {
	bool fileList = false;

	// process modifiers
	bool changed = true;
	while (changed) {
		changed = false;
		for (int i = 0; i < argc; i++) {
			if (strncmp(argv[i], "--", 2) == 0) {
				if (strcasecmp(argv[i], "--file_list") == 0)
					fileList = true;
				else {
					fprintf(stderr, "Illegal parameter: %s\n", argv[i]);
					exit(1);
				}
				for (int k = i; k < argc - 1; k++)
					argv[k] = argv[k + 1];
				argc--;
				changed = true;
			}
		}
	}

	if (argc != 2) {
		fprintf(stderr, "Illegal number of parameters.\n\n");
		fprintf(stderr, "Usage:  GET_TERMID_VECTOR LM_FILE LABEL [--file_list] < TREC_TEXT > TERMID_VECTOR\n\n");
		exit(1);
	}

	LanguageModel lm(argv[0]);
	lm.enableStemmingCache();
	assert(lm.corpusSize > 0);
	assert(lm.documentCount > 0);
	double avgDocLen = lm.corpusSize / lm.documentCount;

	do {	
		char fileName[1024];
		TRECInputStream *inputStream;
		if (fileList) {
			if (scanf("%s", fileName) != 1)
				break;
			inputStream = new TRECInputStream(fileName);
		}
		else
			inputStream = new TRECInputStream(fileno(stdin));

		// read text from stdin and accumulate TF values
		InputToken token;
		map<int,int> tf;
		printf("%s ", argv[1]);
		while (inputStream->getNextToken(&token)) {
			int id = lm.getTermID((char*)token.token);
			if (id < 0)
				continue;
			if (lm.getDocumentProbability(id) > 0.1)
				continue;
			printf(" %d", id + 1);
		}
		delete inputStream;

		if (fileList)
			printf(" # %s", fileName);
		printf("\n");
	} while (fileList);

} // end of getTermIdVector(int, char**)


static const int MY_BUFFER_SIZE = 1024 * 1024;

static void ensureCacheIsFull(int bytesNeeded, char *readBuffer, int &bufferSize, int &bufferPos, int &fd) {
	if (bufferSize < MY_BUFFER_SIZE)
		return;
	if (bufferPos + bytesNeeded <= bufferSize)
		return;
	bufferSize -= bufferPos;
	memmove(readBuffer, &readBuffer[bufferPos], bufferSize);
	bufferPos = 0;
	int result = read(fd, &readBuffer[bufferSize], MY_BUFFER_SIZE - bufferSize);
	if (result > 0)
		bufferSize += result;
} // end of ensureCacheIsFull(int, char*, int&, int&, int&)

static void getIndexStatistics2(int argc, char **argv);  // forward declaration

static void getIndexStatistics(int argc, char **argv) {
	if ((argc < 1) || (argc > 2)) {
		fprintf(stderr, "Usage:  GET_INDEX_STATISTICS INDEX_FILE_NAME [--INCLUDE_GAP_STATS]\n");
		exit(1);
	}
	bool dGapStatistics = false;
	if (argc == 2) {
		if (strcasecmp(argv[1], "--INCLUDE_GAP_STATS") != 0)
			getIndexStatistics(0, NULL);
		dGapStatistics = true;
	}

	if (CompactIndex2::canRead(argv[0])) {
		getIndexStatistics2(argc, argv);
		return;
	}
	
	int fd = open(argv[0], O_RDONLY);
	assert(fd >= 0);
	struct stat buf;
	int status = fstat(fd, &buf);
	assert(status == 0);

	// read header information to find out how much data we have in the index
	CompactIndex_Header header;
	lseek(fd, buf.st_size - sizeof(CompactIndex_Header), SEEK_SET);
	status = read(fd, &header, sizeof(header));
	assert(status == sizeof(header));
	int listCount = header.listCount;
	int termCount = header.termCount;
	lseek(fd, (off_t)0, SEEK_SET);

	offset postings[MAX_SEGMENT_SIZE];
	long long postingCount = 0;
	long long totalSizeOfPostings = 0;
	long long totalTermLength = 0;
	long long dGapCount[256];
	memset(dGapCount, 0, sizeof(dGapCount));

	static const int INDEX_SEGMENT_SIZE = 65536;
	long long lastSegmentStart = 0;
	long long descriptorCount = 0;
	
	char readBuffer[MY_BUFFER_SIZE];
	int bufferSize = read(fd, readBuffer, MY_BUFFER_SIZE);
	int bufferPos = 0;
	int listPos = 0;
	long long filePos = 0;
	char previousTerm[256];
	previousTerm[0] = 0;

	static const int FRONTCODING_GROUP_SIZE = 256;
	char prevTermInGroup[256] = { 0 };
	int frontCodedDictionarySize = 0;
	int termsInCurrentGroup = 0;
	int64_t prevTermFilePos = 0;

	while (listPos < listCount) {
		char currentTerm[256];
		int64_t oldFilePos = filePos;
		ensureCacheIsFull(16384, readBuffer, bufferSize, bufferPos, fd);

		strcpy(currentTerm, &readBuffer[bufferPos]);
		int len = strlen(currentTerm);
		bufferPos += len + 1;
		filePos += len + 1;

		// count the number of dictionary entries in the incomplete in-memory dictionary
		if (oldFilePos > lastSegmentStart + INDEX_SEGMENT_SIZE) {
			lastSegmentStart = oldFilePos;
			descriptorCount++;
	
			// compute impact on front-coded dictionary size
			if (++termsInCurrentGroup > FRONTCODING_GROUP_SIZE) {
				frontCodedDictionarySize += sizeof(int32_t);
				frontCodedDictionarySize += len + 1;
				frontCodedDictionarySize += sizeof(int64_t);
				termsInCurrentGroup = 1;
			}
			else {
				int match = 0;
				while ((currentTerm[match] == prevTermInGroup[match]) && (match < len) && (match < 15))
					match++;
				if (len - match <= 15)
					frontCodedDictionarySize += len - match + 1;
				else
					frontCodedDictionarySize += len - match + 2;
				int64_t delta = oldFilePos - prevTermFilePos;
				while (delta > 0) {
					frontCodedDictionarySize++;
					delta >>= 7;
				}
			}
			strcpy(prevTermInGroup, currentTerm);
			prevTermFilePos = oldFilePos;
		}

		// update term statistics in case this is a new term
		if (strcmp(currentTerm, previousTerm) != 0) {
			strcpy(previousTerm, currentTerm);
			totalTermLength += len;
		}

		int32_t currentSegmentCount;
		memcpy(&currentSegmentCount, &readBuffer[bufferPos], sizeof(int32_t));
		bufferPos += sizeof(int32_t);
		filePos += sizeof(int32_t);
		PostingListSegmentHeader currentHeaders[CompactIndex::MAX_SEGMENTS_IN_MEMORY];
		memcpy(currentHeaders, &readBuffer[bufferPos],
			currentSegmentCount * sizeof(PostingListSegmentHeader));
		bufferPos += currentSegmentCount * sizeof(PostingListSegmentHeader);
		filePos += currentSegmentCount * sizeof(PostingListSegmentHeader);

		for (int k = 0; k < currentSegmentCount; k++) {
			int byteSize = currentHeaders[k].byteLength;
			ensureCacheIsFull(byteSize, readBuffer, bufferSize, bufferPos, fd);

			// collect d-gap statistics if the user asks for it
			if (dGapStatistics) {
				int length, bitCount = 1;
				decompressList((byte*)&readBuffer[bufferPos], byteSize, &length, postings);
				offset prev = 0;
				for (int i = 0; i < length; i++) {
					offset delta = postings[i] - prev;
					while (delta >= (TWO << bitCount))
						bitCount++;
					while (delta < (ONE << bitCount))
						bitCount--;
					dGapCount[bitCount]++;
					prev = postings[i];
				}
			}
			
			bufferPos += byteSize;
			filePos += byteSize;
			listPos++;

			// update postings statistics
			postingCount += currentHeaders[k].postingCount;
			totalSizeOfPostings += currentHeaders[k].byteLength;
		}

	} // end while (listPos < listCount)

	close(fd);

	long long indexSize = buf.st_size;
	printf("Total size of index:    %lld bytes\n", indexSize);
	printf("Number of terms:        %d\n", termCount);
	printf("Number of postings:     %lld\n", postingCount);
	printf("Size of postings:       %lld bytes (%.3lf bits per posting)\n",
			totalSizeOfPostings, totalSizeOfPostings * 8.0 / postingCount);
	if (dGapStatistics)
		printf("D-gaps of size 1,2,...: %lld, %lld, %lld, %lld, %lld\n",
			dGapCount[0], dGapCount[1], dGapCount[2], dGapCount[3], dGapCount[4]);
	printf("Total length of terms:  %lld bytes (%.2lf bytes per term, without terminator)\n",
			totalTermLength, totalTermLength * 1.0 / termCount);
	printf("Overhead per term:      %.2lf bytes\n", (indexSize - totalSizeOfPostings) * 1.0 / termCount);
	printf("----------\n");
	printf("Number of descriptors in incomplete dictionary:  %lld (assuming a segment size of %d bytes)\n",
			descriptorCount, INDEX_SEGMENT_SIZE);
	printf("Size of front-coded incomplete dictionary: %d bytes (assuming group size of %d terms)\n",
			frontCodedDictionarySize, FRONTCODING_GROUP_SIZE);
} // end of getIndexStatistics(int, char**)


static void getIndexStatistics2(int argc, char **argv) {
	int fd = open(argv[0], O_RDONLY);
	assert(fd >= 0);
	struct stat buf;
	int status = fstat(fd, &buf);
	assert(status == 0);
	close(fd);

	int termCount = 0;
	long long postingCount = 0;
	long long totalSizeOfPostings = 0;
	long long totalTermLength = 0;

	char *currentTerm = duplicateString("");

	IndexIterator *iterator = CompactIndex::getIterator(argv[0], 1024 * 1024);
	while (iterator->hasNext()) {
		const char *term = iterator->getNextTerm();
		if (strcmp(currentTerm, term) != 0) {
			free(currentTerm);
			currentTerm = duplicateString(term);
			termCount++;
			totalTermLength += strlen(currentTerm);
		}
		int length, size;
		byte *buffer = iterator->getNextListCompressed(&length, &size, NULL);
		free(buffer);
		postingCount += length;
		totalSizeOfPostings += size;
	}
	delete iterator;

	long long indexSize = buf.st_size;
	printf("Total size of index:    %lld bytes\n", indexSize);
	printf("Number of terms:        %d\n", termCount);
	printf("Number of postings:     %lld\n", postingCount);
	printf("Size of postings:       %lld bytes (%.3lf bits per posting)\n",
			totalSizeOfPostings, totalSizeOfPostings * 8.0 / postingCount);
	printf("Total length of terms:  %lld bytes (%.2lf bytes per term, without terminator)\n",
			totalTermLength, totalTermLength * 1.0 / termCount);
}


static void measureDecodingPerformance(int argc, char **argv) {
	if ((argc < 2) || (argc > 3)) {
		fprintf(stderr, "Usage:  MEASURE_DECODING_PERFORMANCE INDEX_FILE COMPRESSION_METHOD [--IGNORE_STOPWORDS]\n");
		exit(0);
	}

	if (argc == 3) {
		assert(strcasecmp(argv[2], "--ignore_stopwords") == 0);
	}
	bool ignoreStopwords = (argc == 3);

	map<string,double> postingsForTerm, timeForTerm;
	map<string,int> bucketForTerm, byteSizeForTerm;

	static const double MIN_TIME_PER_TERM = 0.01;  // spend at least 10 ms on every term
	static const int MAX_POSTINGS = 32 * 1024;
	offset *postings = new offset[MAX_POSTINGS];

	CompactIndex *index = CompactIndex::getIndex(NULL, argv[0], false);
	int id = getCompressorForName(argv[1]);
	Compressor compressor = compressorForID[id];

	double postingsSeen = 0, timeElapsed = 0, totalByteSize = 0;
	double postingsSeenB[40], timeElapsedB[40];
	for (int i = 0; i < 40; i++)
		postingsSeenB[i] = timeElapsedB[i] = 0;

	// read terms from stdin
	char term[256];
	while (scanf("%s", term) > 0) {
		if ((ignoreStopwords) && (isStopword(term, LANGUAGE_ENGLISH)))
			continue;

		double elapsed = 0, listLength = 0;
		int bucket = 0, byteSize = 0;

		if (postingsForTerm.find(term) != postingsForTerm.end()) {
			listLength = postingsForTerm[term];
			elapsed = timeForTerm[term];
			bucket = bucketForTerm[term];
			byteSize = byteSizeForTerm[term];
		}
		else {
			ExtentList *list = index->getPostings(term);
			listLength = list->getLength();
			int pCnt = list->getNextN(0, MAX_OFFSET, MAX_POSTINGS, postings, postings);
			delete list;
			if (pCnt < 32)
				continue;

			int iterations = 1;
			byte *compressed = compressor(postings, pCnt, &byteSize);
			elapsed = 0;
			while (elapsed < MIN_TIME_PER_TERM) {
				iterations *= 2;
				double startTime = getCurrentTime();
				for (int i = 0; i < iterations; i++) {
					int cnt;
					decompressList(compressed, byteSize, &cnt, postings);
					assert(cnt == pCnt);
				}
				double endTime = getCurrentTime();
				elapsed = (endTime - startTime);
			}
			free(compressed);

			double averageGap = (postings[pCnt - 1] - postings[0]) * 1.0 / (pCnt - 1);
			bucket = (int)(log(averageGap) / log(2) + 0.5);
			elapsed = (elapsed / iterations) / pCnt * listLength;
			byteSize = (int)(byteSize * 1.0 / pCnt * listLength);

			postingsForTerm[term] = listLength;
			timeForTerm[term] = elapsed;
			bucketForTerm[term] = bucket;
			byteSizeForTerm[term] = byteSize;
		}

		totalByteSize += byteSize;
		timeElapsed += elapsed;
		postingsSeen += listLength;
		timeElapsedB[bucket] += elapsed;
		postingsSeenB[bucket] += listLength;
	} // end while (scanf("%s", term) != 0)

	delete index;

	printf("Bucket     Postings seen     Time elapsed     Time per posting\n");
	printf("--------------------------------------------------------------\n");
	for (int i = 0; i < 20; i++)
		printf("%6d     %13.0lf     %8.2lf sec     %13.3lf ns\n",
				i, postingsSeenB[i], timeElapsedB[i], timeElapsedB[i] * 1E9 / postingsSeenB[i]);
	printf("--------------------------------------------------------------\n");
	printf("Total:     %13.0lf     %8.2lf sec     %13.3lf ns\n",
			postingsSeen, timeElapsed, timeElapsed * 1E9 / postingsSeen);
	printf("Total:     %.0lf compressed bytes (%.3lf bits per posting)\n",
			totalByteSize, totalByteSize * 8.0 / postingsSeen);
} // end of measureDecodingPerformance(int, char**)


static void mergeIndices(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Illegal number of parameters. Specify input and output file(s).\n");
		exit(1);
	}
	char *outputFile = argv[argc - 1];
	struct stat buf;
	if (stat(outputFile, &buf) == 0) {
		fprintf(stderr, "Output file already exists. Cowardly refusing to replace it.\n");
		exit(1);
	}
	int inputCount = argc - 1;
	IndexIterator **iterators = typed_malloc(IndexIterator*, inputCount);
	for (int i = 0; i < inputCount; i++) {
		if (stat(argv[i], &buf) != 0) {
			fprintf(stderr, "Input file does not exist: %s\n", argv[i]);
			exit(1);
		}
		iterators[i] = CompactIndex::getIterator(argv[i], MERGE_BUFFER_SIZE / inputCount);
	}
	IndexMerger::mergeIndices(NULL, outputFile, iterators, inputCount);
} // end of mergeIndices(int, char**)


static void recompressIndex(int argc, char **argv) {
	if ((argc < 3) || (argc > 4)) {
		fprintf(stderr, "Illegal number of parameters.\n");
		exit(1);
	}
	struct stat buf;
	if (stat(argv[1], &buf) == 0) {
		fprintf(stderr, "Output file already exists. Cowardly refusing to replace it.\n");
		exit(1);
	}

	IndexIterator *source = CompactIndex::getIterator(argv[0], 4 * 1024 * 1024);
	CompactIndex *target = CompactIndex::getIndex(NULL, argv[1], true);

	// the following is for global Huffman models: compute gap statistics for
	// groups of lists, where the lists in bucket k have length 2^k .. 2^(k+1)-1
	static const int GLOBAL_BUCKET = 19;
	HuffmanStruct models[GLOBAL_BUCKET + 1][40];

	bool huffmanGlobal = false, huffmanMixed = false;
	if (strcasecmp(argv[2], "HUFFMAN_GLOBAL") == 0)
		huffmanGlobal = true;
	if (strcasecmp(argv[2], "HUFFMAN_MIXED") == 0)
		huffmanMixed = true;

	if ((huffmanGlobal) || (huffmanMixed)) {
		// build global Huffman models, for each type of posting list (based on
		// number of postings in list)
		for (int b = 0; b <= GLOBAL_BUCKET; b++)
			for (int g = 0; g < 40; g++) {
				models[b][g].id = g;
				models[b][g].frequency = 0;
			}
		while (source->hasNext()) {
			PostingListSegmentHeader *header = source->getNextListHeader();
			int bucket = (int)(log(header->postingCount) / log(2) + 1E-6);
			int length;
			offset *postings = source->getNextListUncompressed(&length, NULL);
			int bitCount = 1;
			for (int i = 1; i < length; i++) {
				offset delta = postings[i] - postings[i - 1];
				while (delta >= (TWO << bitCount))
					bitCount++;
				while (delta < (ONE << bitCount))
					bitCount--;
				if (bitCount >= 40)
					continue;
				if (++models[bucket][bitCount].frequency > 2000000000)
					for (int g = 0; g < 40; g++)
						models[bucket][g].frequency = (models[bucket][g].frequency + 1) / 2;
				if ((huffmanGlobal || (length < 256)))
					if (++models[GLOBAL_BUCKET][bitCount].frequency > 2000000000)
						for (int g = 0; g < 40; g++)
							models[GLOBAL_BUCKET][g].frequency = (models[GLOBAL_BUCKET][g].frequency + 1) / 2;
			}
			free(postings);
		}

		// build Huffman trees and compute length-limited canonical codes
		for (int b = 0; b <= GLOBAL_BUCKET; b++) {
			doHuffman(models[b], 40);
			restrictHuffmanCodeLengths(models[b], 40, 12);
			computeHuffmanCodesFromCodeLengths(models[b], 40);
			sortHuffmanStructsByID(models[b], 40);
		}

		delete source;
		source = CompactIndex::getIterator(argv[0], 4 * 1024 * 1024);
	} // end if (huffmanGlobal)

	Compressor compressor = NULL;
	if ((!huffmanGlobal) && (!huffmanMixed)) {
		int id = getCompressorForName(argv[2]);
		compressor = compressorForID[id];
	}
	bool verify = false;
	if ((argc == 4) && (strcasecmp(argv[3], "--verify") == 0))
		verify = true;

	// traverse index and recompress every list segment encountered on the way
	while (source->hasNext()) {
		char term[MAX_TOKEN_LENGTH * 2];
		int length, byteSize;
		strcpy(term, source->getNextTerm());
		offset *postings = source->getNextListUncompressed(&length, NULL);
		byte *compressed;
		if (huffmanGlobal) {
			int bucket = (int)(log(length) / log(2) + 1E-6);
			bucket = GLOBAL_BUCKET;
			assert(bucket <= GLOBAL_BUCKET);
			compressed = compressLLRunWithGivenModel(postings, length, models[bucket], &byteSize);
		}
		else if (huffmanMixed) {
			if (length >= 256)
				compressed = compressLLRun(postings, length, &byteSize);
			else
				compressed = compressLLRunWithGivenModel(postings, length, models[GLOBAL_BUCKET], &byteSize);
//			int bucket = (int)(log(length) / log(2) + 1E-6);
//			bucket = GLOBAL_BUCKET;
//			assert(bucket <= GLOBAL_BUCKET);
//			int byteSize2;
//			byte *compressed2 =
//				compressHuffmanWithGivenModel(postings, length, models[bucket], &byteSize2);
//			if (byteSize2 < byteSize) {
//				free(compressed);
//				compressed = compressed2;
//				byteSize = byteSize2;
//			}
//			else
//				free(compressed2);
		}
		else
			compressed = compressor(postings, length, &byteSize);

		if (verify) {
			int len;
			offset *uncompressed = decompressList(compressed, byteSize, &len, NULL);
			assert(len == length);
			for (int i = 0; i < len; i++) {
				if (uncompressed[i] != postings[i]) {
					for (int k = MAX(0, i - 2); k < i; k++) {
						fprintf(stderr, "uncompressed[%d] == postings[%d]: %lld\n",
								k, k, static_cast<long long>(postings[k]));
					}
					fprintf(stderr, "uncompressed[%d] != postings[%d]: %lld != %lld\n",
							i, i, static_cast<long long>(uncompressed[i]), static_cast<long long>(postings[i]));
				}
				assert(uncompressed[i] == postings[i]);
			}
			free(uncompressed);
		} // end if (verify)

		target->setIndexCompressionMode(extractCompressionModeFromList(compressed));
		target->addPostings(term, compressed, byteSize, length, postings[0], postings[length - 1]);
		free(postings);
		free(compressed);
	}
	delete source;
	delete target;
} // end of recompressIndex(int, char**)


static void termIdsToTermStrings(int argc, char **argv) {
	if (argc != 1) {
		fprintf(stderr, "Usage:  TERMIDS_TO_TERMSTRINGS LM_FILE < INPUT > OUTPUT\n\n");
		exit(1);
	}

	int BUFFER_SIZE = 16 * 1024 * 1024;
	char *line = (char*)malloc(BUFFER_SIZE + 1);
	LanguageModel lm(argv[0]);
	while (fgets(line, BUFFER_SIZE, stdin) != NULL) {
		StringTokenizer tok(line, " \t");
		for (char *token = tok.nextToken(); token != NULL; token = tok.nextToken()) {
			int id;
			if (sscanf(token, "%d", &id) == 1) {
				char *term = lm.getTermString(id - 1);
				if (term == NULL)
					continue;
				printf("%s ", term);
				free(term);
			}
		}
		printf("\n");
	}
	free(line);
} // end of termIdsToTermStrings(int, char**)


void tfToTermContrib(int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "Usage:  TF_TO_TERM_CONTRIB BM25_K1 BM25_B BITS_PER_SCORE INPUT_INDEX OUTPUT_INDEX\n\n");
		exit(1);
	}

	// Process parameters.
	double k1;
	assert(sscanf(argv[0], "%lf", &k1) == 1);
	assert(k1 > 0.0);
	double b;
	assert(sscanf(argv[1], "%lf", &b) == 1);
	assert((b >= 0.0) && (b <= 1.0));
	int bitsPerScore;
	assert(sscanf(argv[2], "%d", &bitsPerScore) == 1);
	assert((bitsPerScore >= 1) && (bitsPerScore <= 10));

	char *inputFile = argv[3];
	char *outputFile = argv[4];
	if (!fileExists(inputFile)) {
		fprintf(stderr, "Input file does not exist: %s\n", inputFile);
		exit(1);
	}
	if (fileExists(outputFile)) {
		fprintf(stderr, "Output file already exists: %s\n", outputFile);
		exit(1);
	}

	CompactIndex *inputIndex = CompactIndex::getIndex(NULL, inputFile, false);
	IndexIterator *inputIterator = CompactIndex::getIterator(inputFile, 1 << 20);
	CompactIndex *outputIndex = CompactIndex::getIndex(NULL, outputFile, true);

	// Obtain doclens for all documents in the index. Also compute avgdl.
	ExtentList_FromTo documents(
			inputIndex->getPostings("<doc>"), inputIndex->getPostings("</doc>"));
	int documentCount = documents.getLength();
	assert(documentCount > 0);
	float *documentLengths = typed_malloc(float, documentCount);
	double avgdl;
	int outPos = 0;
	offset s = -1, e;
	while (documents.getFirstStartBiggerEq(s + 1, &s, &e)) {
		float dl = (e - s - 1);
		documentLengths[outPos++] = dl;
		avgdl += dl;
	}
	assert(outPos == documentCount);
	avgdl /= documentCount;

	char currentTerm[64];
	const int MAX_POSTING_COUNT = documentCount + 2048;
	offset *postings = typed_malloc(offset, MAX_POSTING_COUNT);
	int pCnt = 0;
	while (inputIterator->hasNext()) {
		strcpy(currentTerm, inputIterator->getNextTerm());
		int cnt;
		inputIterator->getNextListUncompressed(&cnt, postings + pCnt);
		pCnt += cnt;
		assert(pCnt <= MAX_POSTING_COUNT);
		char *nextTerm = inputIterator->getNextTerm();
		if ((nextTerm == NULL) || (strcmp(nextTerm, currentTerm) != 0)) {
			// We have read all postings for the current term. Process it.
			if (startsWith(currentTerm, "<!>")) {
				// Document-level posting list.
				for (int i = 0; i < pCnt; i++) {
					offset docid = (postings[i] >> DOC_LEVEL_SHIFT);
					assert((docid >= 0) && (docid < documentCount));
					offset tf = decodeDocLevelTF(postings[i] & DOC_LEVEL_MAX_TF);
					double score = BM25Query::getScore(tf, k1, b, documentLengths[docid], avgdl);
					double maxScorePossible = k1 + 1.0;
					offset discretizedScore =
							static_cast<offset>(score / maxScorePossible * (1 << bitsPerScore));
					assert((discretizedScore >= 0) && (discretizedScore < (1 << bitsPerScore)));
					if (bitsPerScore >= DOC_LEVEL_SHIFT) {
						postings[i] = (docid << bitsPerScore) + discretizedScore;
					}
					else {
						// We have to insert artificial 0 bits here, for otherwise we will break
						// compressHuffman2 (which will then revert to compressHuffman).
						postings[i] = (docid << DOC_LEVEL_SHIFT) + discretizedScore;
					}
				}
				outputIndex->addPostings(currentTerm, postings, pCnt);
			}
			else {
				// "<doc>" or "</doc>"
				outputIndex->addPostings(currentTerm, postings, pCnt);
			}
			pCnt = 0;
		}
	}

	delete inputIndex;
	delete inputIterator;
	delete outputIndex;
	free(documentLengths);
	free(postings);
}


int main(int argc, char **argv) {
	initializeConfigurator(NULL, NULL);
	if (argc < 2)
		usage();
	if (strcasecmp(argv[1], "--debug") == 0) {
		setLogLevel(LOG_DEBUG);
		argv++;
		argc--;
	}
	if (strcasecmp(argv[1], "STEMMING") == 0)
		stemming();
	else if (strcasecmp(argv[1], "BUILD_LM") == 0)
		buildLanguageModel(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "BUILD_INDEX_FROM_ASCII") == 0)
		buildIndexFromASCII(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "BUILD_DOCUMENT_LENGTH_VECTOR") == 0)
		buildDocumentLengthVector(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "CREATE_EMPTY_INDEX") == 0)
		createEmptyIndex(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "COMPRESS_LISTS") == 0)
		compressLists(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "EXTRACT_DOCIDS") == 0)
		extractDocumentIDs(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "EXTRACT_POSTINGS") == 0)
		extractPostings(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "EXTRACT_VOCAB") == 0)
		extractVocabularyTerms(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "FINALIZE_PRUNED_INDEX") == 0)
		finalizePrunedIndex(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "GET_COMPRESSION_STATS") == 0)
		getCompressionStats(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "GET_DOCUMENT_INDEX") == 0)
		getDocumentIndex(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "GET_FEATURE_VECTOR") == 0)
		getFeatureVector(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "GET_TERMID_VECTOR") == 0)
		getTermIdVector(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "GET_INDEX_STATISTICS") == 0)
		getIndexStatistics(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "MEASURE_DECODING_PERFORMANCE") == 0)
		measureDecodingPerformance(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "MERGE_INDICES") == 0)
		mergeIndices(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "RECOMPRESS_INDEX") == 0)
		recompressIndex(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "TERMIDS_TO_TERMSTRINGS") == 0)
		termIdsToTermStrings(argc - 2, &argv[2]);
	else if (strcasecmp(argv[1], "TF_TO_TERM_CONTRIB") == 0)
		tfToTermContrib(argc - 2, &argv[2]);
	else
		usage();
	return 0;
} // end of main(int, char**)


