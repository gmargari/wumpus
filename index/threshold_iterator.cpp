/**
 * Implementation of the ThresholdIterator class. See header file for documentation.
 *
 * author: Stefan Buettcher
 * created: 2007-02-15
 * changed: 2007-02-15
 **/


#include <string.h>
#include "threshold_iterator.h"
#include "../misc/all.h"


static const char *LOG_ID = "ThresholdIterator";


ThresholdIterator::ThresholdIterator(IndexIterator *iterator, int lowerLimit, int upperLimit) {
	assert(iterator != NULL);
	this->iterator = iterator;

	if (lowerLimit > MIN_SEGMENT_SIZE)
		lowerLimit = MIN_SEGMENT_SIZE;
	if (upperLimit < lowerLimit)
		upperLimit = lowerLimit;
	this->lowerLimit = lowerLimit;
	this->upperLimit = upperLimit;

	currentTerm[0] = 0;
	jumpToNext();
} // end of ThresholdIterator(IndexIterator*, int, int)


ThresholdIterator::~ThresholdIterator() {
	delete iterator;
	iterator = NULL;
} // end of ~ThresholdIterator()


int64_t ThresholdIterator::getTermCount() {
	return iterator->getTermCount();
}


int64_t ThresholdIterator::getListCount() {
	return iterator->getListCount();
}


bool ThresholdIterator::hasNext() {
	return (currentHeader != NULL);
}


PostingListSegmentHeader * ThresholdIterator::getNextListHeader() {
	return currentHeader;
}


char * ThresholdIterator::getNextTerm() {
	if (currentHeader == NULL)
		return NULL;
	else
		return currentTerm;
} // end of getNextTerm()


void ThresholdIterator::jumpToNext() {
	while ((currentHeader = iterator->getNextListHeader()) != NULL) {
		if (strcmp(currentTerm, iterator->getNextTerm()) == 0)
			return;
		if ((currentHeader->postingCount >= lowerLimit) && (currentHeader->postingCount <= upperLimit)) {
			strcpy(currentTerm, iterator->getNextTerm());
			return;
		}
		iterator->skipNext();
	}
} // end of jumpToNext()


byte * ThresholdIterator::getNextListCompressed(int *length, int *size, byte *buffer) {
	byte *result = iterator->getNextListCompressed(length, size, buffer);
	jumpToNext();
	return result;
} // end of getNextListCompressed(int*, int*, byte*)


offset * ThresholdIterator::getNextListUncompressed(int *length, offset *buffer) {
	offset *result = iterator->getNextListUncompressed(length, buffer);
	jumpToNext();
	return result;
} // end of getNextListUncompressed(int*, offset*)


void ThresholdIterator::skipNext() {
	iterator->skipNext();
	currentTerm[0] = 0;
	jumpToNext();
} // end of skipNext()


char * ThresholdIterator::getClassName() {
	return duplicateString(LOG_ID);
} // end of getClassName()



