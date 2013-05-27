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
 * Implementation of the IndexIterator2 class.
 *
 * author: Stefan Buettcher
 * created: 2007-07-14
 * changed: 2007-07-16
 **/


#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "index_iterator2.h"
#include "compactindex2.h"
#include "index_types.h"
#include "index_compression.h"
#include "../misc/all.h"


static const char *LOG_ID = "IndexIterator2";

static const char *CI2_HEADER_SIGNATURE = "Wumpus:CompactIndex2";


IndexIterator2::IndexIterator2(const char *fileName, int bufferSize) {
	assert(fileName != NULL);
	snprintf(errorMessage, sizeof(errorMessage), "Creating iterator for index file: %s", fileName);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	this->fileName = duplicateString(fileName);
	bufferSize = MAX(MIN_BUFFER_SIZE, MIN(MAX_BUFFER_SIZE, bufferSize));
	maxBufferSize = bufferSize;
	readBuffer = (byte*)malloc(bufferSize);
	fileHandle = open(fileName, O_RDONLY);
	if (fileHandle < 0) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to obtain iterator for on-disk index: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		perror(NULL);
		listPos = 0;
		listCount = 0;
		termCount = 0;
		return;
	}
	struct stat buf;
	fstat(fileHandle, &buf);
	CompactIndex2_Header header;
	lseek(fileHandle, buf.st_size - sizeof(header), SEEK_SET);
	forced_read(fileHandle, &header, sizeof(header));
	listCount = header.listCount;
	termCount = header.termCount;

	lseek(fileHandle, CI2_SIGNATURE_LENGTH, SEEK_SET);
	this->bufferSize = forced_read(fileHandle, readBuffer, maxBufferSize);
	this->bufferPos = 0;
	this->listPos = 0;
	currentTerm[0] = 0;

	if (listPos < listCount)
		loadNextTerm();
} // end of IndexIterator(char*, int)


IndexIterator2::~IndexIterator2() {
	FREE_AND_SET_TO_NULL(readBuffer);
	FREE_AND_SET_TO_NULL(fileName);
	if (fileHandle >= 0) {
		close(fileHandle);
		fileHandle = -1;
	}
} // end of ~IndexIterator()


void IndexIterator2::loadNextTerm() {
	ensureCacheIsFull(256);

	char term[MAX_TOKEN_LENGTH * 2];
	if (listPos == 0) {
		// first list always starts with term string
		bufferPos += decodeFrontCoding(&readBuffer[bufferPos], currentTerm, term);
		strcpy(currentTerm, term);
		segmentsSeen = 0;
	}
	else if (readBuffer[bufferPos++] == 255) {
		// continuation flag tells us that we are still working on the same term
		if (segmentsSeen == 1) {
			// remove marker from input buffer
			int64_t markerValue;
			bufferPos += sizeof(markerValue);
		}
	}
	else {
		// this is a new term; skip list of sync points for previous term
		// (if present) and load term string into "currentTerm"
		if (segmentsSeen >= 2) {
			int32_t segmentCount, segmentSize;
			bufferPos += decodeVByte32(&segmentCount, &readBuffer[bufferPos]);
			bufferPos += decodeVByte32(&segmentSize, &readBuffer[bufferPos]);
			while (bufferPos + segmentSize > bufferSize) {
				ensureCacheIsFull(maxBufferSize);
				int toIncrease = MIN(segmentSize, bufferSize - bufferPos);
				segmentSize -= toIncrease;
				bufferPos += toIncrease;
			}
			bufferPos += segmentSize;
		}
		ensureCacheIsFull(256);
		bufferPos += decodeFrontCoding(&readBuffer[bufferPos], currentTerm, term);
		strcpy(currentTerm, term);
		segmentsSeen = 0;
	}

	// extract list header for next list segment from compressed input buffer
	offset referencePosting =
		(segmentsSeen == 0 ? 0 : currentHeaders[0].lastElement);
	bufferPos += CompactIndex::decompressPLSH(
			&readBuffer[bufferPos], referencePosting, &currentHeaders[0]);
	segmentsSeen++;

	// these two always need to be re-set to 0 and 1; otherwise, the methods
	// in the IndexIterator class will not function properly
	currentSegmentPos = 0;
	currentSegmentCount = 1;
} // end of loadNextTerm()


char * IndexIterator2::getClassName() {
	return duplicateString(LOG_ID);
}


