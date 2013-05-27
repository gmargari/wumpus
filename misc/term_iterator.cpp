/**
 * Implementation of the TermBuffer class.
 *
 * author: Stefan Buettcher
 * created: 2007-02-11
 * changed: 2007-02-11
 **/


#include <string.h>
#include "term_iterator.h"
#include "../misc/all.h"


static const char *LOG_ID = "TermIterator";


TermIterator::TermIterator() {
	allocated = INITIAL_ALLOCATION;
	used = consumed = 0;
	termBuffer = (char*)malloc(allocated);
	prevTerm[0] = lastTerm[0] = 0;
	maxTermLength = 0;
} // end of TermIterator()


TermIterator::~TermIterator() {
	if (termBuffer != NULL) {
		free(termBuffer);
		termBuffer = 0;
	}
} // end of ~TermBuffer()


int TermIterator::getMaxTermLength() {
	return maxTermLength;
}


void TermIterator::addTerm(const char *term) {
	int len = strlen(term);
	if (len > MAX_LENGTH) {
		log(LOG_ERROR, LOG_ID, "Term too long.");
		assert(len <= MAX_LENGTH);
	}
	if (allocated < used + len + 4) {
		allocated *= 2;
		termBuffer = (char*)realloc(termBuffer, allocated);
	}

	// find common prefix
	int prefixLen = 0;
	while ((prefixLen < 15) && (term[prefixLen] == lastTerm[prefixLen]) && (term[prefixLen]))
		prefixLen++;

	// encode prefix and suffix length
	int suffixLen = len - prefixLen;
	if (suffixLen < 16)
		termBuffer[used++] = prefixLen | (suffixLen << 4);
	else {
		assert(len < 256);
		termBuffer[used++] = prefixLen | (15 << 4);
		termBuffer[used++] = suffixLen;
	}

	// copy suffix to term buffer
	memcpy(&termBuffer[used], &term[prefixLen], len - prefixLen);
	used += len - prefixLen;

	// update internal state
	memcpy(&lastTerm[prefixLen], &term[prefixLen], suffixLen);
	if (len > maxTermLength)
		maxTermLength = len;
} // end of addTerm(char*)


char * TermIterator::getNext(char *buffer) {
	if (consumed == used)
		return NULL;

	// obtain prefix and suffix length
	int temp = termBuffer[consumed++];
	int prefixLen = (temp & 15);
	int suffixLen = (temp >> 4);
	if (suffixLen == 15)
		suffixLen = termBuffer[consumed++];

	// allocate memory for term
	if (buffer == NULL)
		buffer = (char*)malloc(prefixLen + suffixLen + 1);

	// copy prefix and suffix to output buffer
	memcpy(buffer, prevTerm, prefixLen);
	memcpy(&buffer[prefixLen], termBuffer, suffixLen);
	buffer[prefixLen + suffixLen] = 0;
	consumed += suffixLen;

	// update internal state
	memcpy(&prevTerm[prefixLen], &buffer[prefixLen], suffixLen);
} // end of getNext(char*)


