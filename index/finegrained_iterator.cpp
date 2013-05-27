/**
 * Implementation of the FineGrainedIterator class.
 *
 * author: Stefan Buettcher
 * created: 2007-04-03
 * changed: 2007-04-03
 **/


#include <string.h>
#include "finegrained_iterator.h"
#include "index_iterator.h"
#include "../misc/all.h"


FineGrainedIterator::FineGrainedIterator(IndexIterator *iterator) {
	this->iterator = iterator;
	currentSegment = typed_malloc(offset, MAX_SEGMENT_SIZE + 1);
	advanceToNextTerm();
}


FineGrainedIterator::~FineGrainedIterator() {
	free(currentSegment);
	currentSegment = NULL;
	delete iterator;
	iterator = NULL;
} // end of ~FineGrainedIterator()


void FineGrainedIterator::advanceToNextTerm() {
	char *term = iterator->getNextTerm();
	if (term == NULL)
		currentTerm[0] = 0;
	else {
		assert(strcmp(currentTerm, term) != 0);
		strcpy(currentTerm, term);
	}
	currentSegmentPos = 0;
	iterator->getNextListUncompressed(&currentSegmentSize, currentSegment);
	assert(currentSegmentSize <= MAX_SEGMENT_SIZE);
} // end of advanceToNextTerm()


char * FineGrainedIterator::getCurrentTerm() {
	return currentTerm;
} // end of getCurrentTerm()


void FineGrainedIterator::reload() {
	char *term = iterator->getNextTerm();
	if (term == NULL)
		currentSegmentPos = currentSegmentSize = 0;
	else if (strcmp(term, currentTerm) != 0)
		currentSegmentPos = currentSegmentSize = 0;
	else {
		currentSegmentPos = 0;
		iterator->getNextListUncompressed(&currentSegmentSize, currentSegment);
		assert(currentSegmentSize <= MAX_SEGMENT_SIZE);
	}
} // end of reload()


offset FineGrainedIterator::getNextPosting() {
	if ((currentTerm[0] == 0) || (currentSegmentSize == 0))
		return MAX_OFFSET;
	offset result = currentSegment[currentSegmentPos];
	if (++currentSegmentPos >= currentSegmentSize)
		reload();
} // end of getNextPosting()


int FineGrainedIterator::getNextNPostings(int n, offset *buffer) {
	if ((currentTerm[0] == 0) || (currentSegmentSize == 0))
		return 0;
	if (currentSegmentPos + n <= currentSegmentSize) {
		memcpy(buffer, &currentSegment[currentSegmentPos], n * sizeof(offset));
		currentSegmentPos += n;
		if (currentSegmentPos >= currentSegmentSize)
			reload();
		return n;
	}
	else {
		int m = currentSegmentSize - currentSegmentPos;
		memcpy(buffer, &currentSegment[currentSegmentPos], m * sizeof(offset));
		reload();
		return m + getNextNPostings(n - m, &buffer[m]);
	}
} // end of getNextNPostings(int, offset*)






