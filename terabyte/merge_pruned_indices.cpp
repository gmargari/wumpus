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
 * Usage:  merge_pruned_indices INPUT_1 .. INPUT_N OUTPUT
 *
 * Merges the pruned input indices into a pruned index whose file name is
 * given by OUTPUT.
 *
 * author: Stefan Buettcher
 * created: 2005-10-20
 * changed: 2007-07-13
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terabyte.h"
#include "../index/compactindex.h"
#include "../index/index_iterator.h"
#include "../index/index_types.h"
#include "../index/multiple_index_iterator.h"
#include "../misc/all.h"


#define TOTAL_BUFFER_SIZE (32 * 1024 * 1024)

#define INITIAL_BUFFER_SIZE (1000000)

#define MAX_POSTINGS_PER_TERM 10000000


static void usage() {
	fprintf(stderr, "Usage:  merge_pruned_indices INPUT_1 .. INPUT_N OUTPUT\n\n");
	fprintf(stderr, "Merges the pruned input indices into a pruned index whose file name ");
	fprintf(stderr, "is given by OUTPUT.\n");
	exit(1);
} // end of usage()


static int cleanUp(char *term, offset *buffer, int length) {
	if (length == 0)
		return 0;
	sortOffsetsAscending(buffer, length);
	int result = 1;
	for (int i = 1; i < length; i++)
		if (buffer[i] != buffer[result - 1])
			buffer[result++] = buffer[i];
	if (result > 0)
		if (buffer[result - 1] < DOCUMENT_COUNT_OFFSET) {
			fprintf(stderr, "No document frequency value found for term \"%s\". Dropping.\n", term);
			result = 0;
		}
	if (result > MAX_POSTINGS_PER_TERM)
		result = MAX_POSTINGS_PER_TERM;
	if (result > 2) {
		if (buffer[result - 1] >= DOCUMENT_COUNT_OFFSET)
			if (buffer[result - 2] >= DOCUMENT_COUNT_OFFSET) {
				fprintf(stderr, "Error: Inconsistent document frequency values found for the same term.\n");
				fprintf(stderr, "\"%s\": " OFFSET_FORMAT " != " OFFSET_FORMAT ". Dropping.\n",
						term, buffer[result - 1], buffer[result - 2]);
				fprintf(stderr, "result = %d\n", result);
				exit(1);
			}
	}
	else
		result = 0;
	return result;
} // end of cleanUp(offset*, int)


static void mergePrunedIndices(IndexIterator *iterator, CompactIndex *target) {
	offset *buffer = typed_malloc(offset, INITIAL_BUFFER_SIZE);
	int bufferSize = INITIAL_BUFFER_SIZE;
	int bufferPos = 0;
	char currentTerm[MAX_TOKEN_LENGTH * 2];
	currentTerm[0] = 0;

	fprintf(stderr, "Starting merge process with buffer size %d.\n", bufferSize);

	while (iterator->hasNext()) {
		char *term = iterator->getNextTerm();
		if (strcmp(term, currentTerm) != 0) {
			if (bufferPos > 2) {
				bufferPos = cleanUp(currentTerm, buffer, bufferPos);
				if (bufferPos > 2)
					target->addPostings(currentTerm, buffer, bufferPos);
			}
			bufferPos = 0;
			strcpy(currentTerm, term);
		}
		PostingListSegmentHeader *header = iterator->getNextListHeader();
		if (bufferPos + header->postingCount >= bufferSize) {
			fprintf(stderr, "Increasing buffer size to %d postings.\n", bufferSize * 2);
			bufferSize = bufferSize * 2;
			buffer = typed_realloc(offset, buffer, bufferSize);
		}
		int length;
		iterator->getNextListUncompressed(&length, &buffer[bufferPos]);
		assert(bufferPos + length < bufferSize);
		if ((term[0] != '<') || (term[1] == '!'))
			bufferPos += length;
	} // end while (iterator->hasNext())

	if (bufferPos > 2) {
		bufferPos = cleanUp(currentTerm, buffer, bufferPos);
		if (bufferPos > 2)
			target->addPostings(currentTerm, buffer, bufferPos);
	}
	free(buffer);
} // end of mergePrunedIndices(IndexIterator*, CompactIndex*)


int main(int argc, char **argv) {
	if (argc < 3)
		usage();
	char *outputFile = argv[argc - 1];
	struct stat buf;
	if (stat(outputFile, &buf) == 0) {
		fprintf(stderr, "Output file already exists. Cowardly refusing to run.\n");
		exit(1);
	}
	int inputCount = argc - 2;
	char **inputFiles = &argv[1];
	IndexIterator **iterators = typed_malloc(IndexIterator*, inputCount);
	for (int i = 0; i < inputCount; i++) {
		if (stat(inputFiles[i], &buf) != 0) {
			fprintf(stderr, "Input file does not exist: %s\n", inputFiles[i]);
			exit(1);
		}
		iterators[i] = CompactIndex::getIterator(inputFiles[i], TOTAL_BUFFER_SIZE);
	}

	IndexIterator *iterator = new MultipleIndexIterator(iterators, inputCount);
	CompactIndex *target = CompactIndex::getIndex(NULL, outputFile, true);
	mergePrunedIndices(iterator, target);
	delete target;
	delete iterator;

	return 0;
} // end of main(int, char**)


