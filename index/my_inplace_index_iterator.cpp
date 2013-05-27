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
 * Implementation of the InPlaceIndexIterator class.
 *
 * author: Stefan Buettcher
 * created: 2005-11-22
 * changed: 2006-05-01
 **/


#include <string.h>
#include "inplace_index_iterator.h"
#include "inplace_index.h"
#include "../misc/all.h"


static char * LOG_ID = "InPlaceIndexIterator";


InPlaceIndexIterator::InPlaceIndexIterator(InPlaceIndex *index, int bufferSize) {
	if (bufferSize < MIN_BUFFER_SIZE)
		bufferSize = MIN_BUFFER_SIZE;
	if (bufferSize > MAX_BUFFER_SIZE)
		bufferSize = MAX_BUFFER_SIZE;
	this->maxBufferSize = bufferSize;
	this->index = index;
	char *fileName = index->getFileName();
	fileHandle = open(fileName, O_RDONLY);
	free(fileName);
	if (fileHandle < 0) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to open input file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		readBuffer = NULL;
	}
	else {
		readBuffer = (byte*)malloc(maxBufferSize);
		this->bufferPos = this->bufferSize = 0;
		this->termCount = index->getTermCount();
	}
} // end of InPlaceIndexIterator(char*, int)


InPlaceIndexIterator::~InPlaceIndexIterator() {
	if (fileHandle >= 0) {
		close(fileHandle);
		fileHandle = -1;
	}
	if (readBuffer != NULL) {
		free(readBuffer);
		readBuffer = NULL;
	}
} // end of ~InPlaceIndexIterator()


int64_t InPlaceIndexIterator::getTermCount() {
	return termCount;
}


int64_t InPlaceIndexIterator::getListCount() {
	int result = 0;
	for (int i = 0; i < termCount; i++)
		result += terms[i]->segmentCount;
	return result;
} // end of getListCount()


bool InPlaceIndexIterator::hasNext() {
	return (currentTerm < termCount);
}


char * InPlaceIndexIterator::getNextTerm() {
	if (currentTerm >= termCount)
		return NULL;
	else
		return terms[currentTerm]->term;
}


PostingListSegmentHeader * InPlaceIndexIterator::getNextListHeader() {
	if (currentTerm >= termCount)
		return NULL;
	else
		return (PostingListSegmentHeader*)&readBuffer[bufferPos];
} // end of getNextListHeader()


byte * InPlaceIndexIterator::getNextListCompressed(int *length, int *size, byte *buffer) {
	if (currentTerm >= termCount) {
		*length = *size = 0;
		return NULL;
	}
	PostingListSegmentHeader *plsh = (PostingListSegmentHeader*)&readBuffer[bufferPos];
	if (buffer == NULL)
		buffer = (byte*)malloc(plsh->byteLength);
	bufferPos += sizeof(PostingListSegmentHeader);
	memcpy(buffer, &readBuffer[bufferPos], plsh->byteLength);
	*length = plsh->postingCount;
	*size = plsh->byteLength;
	bufferPos += plsh->byteLength;
	refillBufferIfNecessary();
	return buffer;
} // end of getNextListCompressed(int*, int*, byte*)


offset * InPlaceIndexIterator::getNextListUncompressed(int *length, offset *buffer) {
	if (currentTerm >= termCount) {
		*length = 0;
		return NULL;
	}
	PostingListSegmentHeader *plsh = (PostingListSegmentHeader*)&readBuffer[bufferPos];
	if (buffer == NULL)
		buffer = typed_malloc(offset, plsh->postingCount);
	bufferPos += sizeof(PostingListSegmentHeader);
	decompressList(&readBuffer[bufferPos], plsh->byteLength, length, buffer);
	bufferPos += plsh->byteLength;
	refillBufferIfNecessary();
	return buffer;
} // end of getNextListUncompressed(int*, offset*)


void InPlaceIndexIterator::skipNext() {
}


char * InPlaceIndexIterator::getClassName() {
	return duplicateString("InPlaceIndexIterator");
}


void InPlaceIndexIterator::refillBufferIfNecessary() {
} // end of refillBufferIfNecessary()





