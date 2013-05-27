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
 * Implementation of the IndexIterator class.
 *
 * author: Stefan Buettcher
 * created: 2005-04-11
 * changed: 2007-07-13
 **/


#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "index_iterator.h"
#include "index_types.h"
#include "index_compression.h"
#include "../misc/all.h"


static const char *LOG_ID = "IndexIterator";


IndexIterator::IndexIterator() {
	readBuffer = NULL;
	fileHandle = -1;
	listPos = listCount = 0;
	termCount = 0;
	fileName = NULL;
} // end of IndexIterator()


IndexIterator::IndexIterator(const char *fileName, int bufferSize) {
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
	CompactIndex_Header header;
	lseek(fileHandle, buf.st_size - sizeof(header), SEEK_SET);
	forced_read(fileHandle, &header, sizeof(header));
	listCount = header.listCount;
	termCount = header.termCount;
	lseek(fileHandle, (off_t)0, SEEK_SET);
	this->bufferSize = forced_read(fileHandle, readBuffer, maxBufferSize);
	this->bufferPos = 0;
	this->listPos = 0;
	
	if (listPos < listCount)
		loadNextTerm();
} // end of IndexIterator(char*, int)


IndexIterator::~IndexIterator() {
	FREE_AND_SET_TO_NULL(readBuffer);
	FREE_AND_SET_TO_NULL(fileName);
	if (fileHandle >= 0) {
		close(fileHandle);
		fileHandle = -1;
	}
} // end of ~IndexIterator()


void IndexIterator::ensureCacheIsFull(int bytesNeeded) {
	if ((bufferSize < maxBufferSize) || (bufferPos + bytesNeeded <= bufferSize))
		return;
	bufferSize -= bufferPos;
	memmove(readBuffer, &readBuffer[bufferPos], bufferSize);
	bufferPos = 0;
	int result = forced_read(fileHandle, &readBuffer[bufferSize], maxBufferSize - bufferSize);
	if (result > 0)
		bufferSize += result;
} // end of ensureCacheIsFull(int)


void IndexIterator::loadNextTerm() {
	ensureCacheIsFull(16384);
	strcpy(currentTerm, (char*)&readBuffer[bufferPos]);
	bufferPos += strlen(currentTerm) + 1;

	// use memcpy to read "currentSegmentCount", as it might not be properly aligned
	assert(sizeof(currentSegmentCount) == sizeof(int32_t));
	memcpy(&currentSegmentCount, &readBuffer[bufferPos], sizeof(int32_t));
	bufferPos += sizeof(int32_t);
#if INDEX_MUST_BE_WORD_ALIGNED
	if (bufferPos & 7)
		bufferPos += 8 - (bufferPos & 7);
#endif
	memcpy(currentHeaders, &readBuffer[bufferPos],
			currentSegmentCount * sizeof(PostingListSegmentHeader));
	bufferPos += currentSegmentCount * sizeof(PostingListSegmentHeader);
	currentSegmentPos = 0;
} // end of loadNextTerm()


int64_t IndexIterator::getListCount() {
	return listCount;
}


int64_t IndexIterator::getTermCount() {
	return termCount;
}


bool IndexIterator::hasNext() {
	return (listPos < listCount);
}


char * IndexIterator::getNextTerm() {
	return (listPos < listCount ? currentTerm : NULL);
}


PostingListSegmentHeader * IndexIterator::getNextListHeader() {
	return (listPos < listCount ? &currentHeaders[currentSegmentPos] : NULL);
}


byte * IndexIterator::getNextListCompressed(int *length, int *size, byte *buffer) {
	if (listPos >= listCount) {
		*length = 0;
		*size = 0;
		return NULL;
	}

	int byteSize = currentHeaders[currentSegmentPos].byteLength;
	if (buffer == NULL)
		buffer = (byte*)malloc(byteSize);
	*length = currentHeaders[currentSegmentPos].postingCount;
	*size = byteSize;

	// copy compressed postings data from read cache into output buffer;
	// we might need to do this multiple times, because the read buffer may
	// be smaller than a compressed posting list segment
	int fetched = 0;
	do {
		ensureCacheIsFull(byteSize - fetched);
		int chunkSize = MIN(byteSize - fetched, bufferSize - bufferPos);
		memcpy(&buffer[fetched], &readBuffer[bufferPos], chunkSize);
		bufferPos += chunkSize;
		fetched += chunkSize;
	} while (fetched < byteSize);

	if (++listPos < listCount)
		if (++currentSegmentPos >= currentSegmentCount)
			loadNextTerm();

	return buffer;
} // end of getNextListCompressed(int*, int*, byte*)


offset * IndexIterator::getNextListUncompressed(int *length, offset *buffer) {
	if (listPos >= listCount) {
		*length = 0;
		return NULL;
	}

	int byteSize = currentHeaders[currentSegmentPos].byteLength;
	*length = currentHeaders[currentSegmentPos].postingCount;

	// distinguish between two cases: if the read buffer is big enough to hold
	// compressed list segments, we are fine; otherwise, we have to load the
	// compressed postings into a temporary buffer and decompress from there
	ensureCacheIsFull(byteSize);
	if (bufferPos + byteSize <= bufferSize) {
		buffer = decompressList((byte*)&readBuffer[bufferPos], byteSize, length, buffer);
		bufferPos += byteSize;
		if (++listPos < listCount)
			if (++currentSegmentPos >= currentSegmentCount)
				loadNextTerm();
	}
	else {
		int len, size;
		byte *compressed = getNextListCompressed(&len, &size, NULL);
		buffer = decompressList(compressed, byteSize, length, buffer);
		assert(*length == len);
		free(compressed);
	}

	return buffer;
} // end of getNextListUncompressed(int*, offset*)


void IndexIterator::skipNext() {
	if (listPos >= listCount)
		return;
	int byteSize = currentHeaders[currentSegmentPos].byteLength;
	while (bufferPos + byteSize > bufferSize) {
		ensureCacheIsFull(maxBufferSize);
		int toIncrease = MIN(byteSize, bufferSize - bufferPos);
		byteSize -= toIncrease;
		bufferPos += toIncrease;
	}
	bufferPos += byteSize;
	if (++listPos < listCount)
		if (++currentSegmentPos >= currentSegmentCount)
			loadNextTerm();
} // end of skipNext()


char * IndexIterator::getClassName() {
	return duplicateString(LOG_ID);
}


