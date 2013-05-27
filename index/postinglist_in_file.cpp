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
 * Implementation of the PostingListInFile class.
 *
 * author: Stefan Buettcher
 * created: 2005-08-05
 * changed: 2007-02-12
 **/


#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "postinglist_in_file.h"
#include "compactindex.h"
#include "postinglist.h"
#include "segmentedpostinglist.h"
#include "../filesystem/filefile.h"
#include "../misc/all.h"


static const char *LOG_ID = "PostingListInFile";


PostingListInFile::PostingListInFile(char *fileName) {
	fileHandle = open(fileName, O_RDWR | O_CREAT | O_LARGEFILE, DEFAULT_FILE_PERMISSIONS);
	if (fileHandle < 0) {
		char errorMessage[256];
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to open/create in-place index file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		assert(fileHandle >= 0);
		return;
	}
	this->modified = false;
	this->segmentHeaders = NULL;
	this->lastSegmentIsInMemory = false;
	this->lastSegment = NULL;
	this->fileSizeWithoutHeaders = 0;
	this->postingCount = 0;
	this->segmentCount = 0;
	this->fileName = duplicateString(fileName);

	struct stat buf;
	int status = fstat(fileHandle, &buf);
	assert(status == 0);
	fileSize = buf.st_size;

	if (fileSize == 0) {
		// newly created file: initialize!
		forced_write(fileHandle, &segmentCount, sizeof(segmentCount));
		segmentsAllocated = 8;
		segmentHeaders = typed_malloc(PostingListSegmentHeader, segmentsAllocated);
	}
	else {
		// file already exists: load meta-data into memory
		lseek(fileHandle, fileSize - sizeof(segmentCount), SEEK_SET);
		forced_read(fileHandle, &segmentCount, sizeof(segmentCount));

		if (segmentCount >= 0) {
			// allocate memory for segment headers
			segmentsAllocated = segmentCount + 8;
			segmentHeaders = typed_malloc(PostingListSegmentHeader, segmentsAllocated);

			// load segment headers into memory
			int headerSize = segmentCount * sizeof(PostingListSegmentHeader);
			lseek(fileHandle, fileSize - sizeof(segmentCount) - headerSize, SEEK_SET);
			forced_read(fileHandle, segmentHeaders, headerSize);
			for (int i = 0; i < segmentCount; i++) {
				fileSizeWithoutHeaders += segmentHeaders[i].byteLength;
				postingCount += segmentHeaders[i].postingCount;
			}
			assert(fileSizeWithoutHeaders == fileSize - headerSize - sizeof(segmentCount));
		}
		else {
			// only a single segment on disk: load into memory and uncompress
			int byteLength = -segmentCount;
			byte *compressed = (byte*)malloc(byteLength);
			lseek(fileHandle, 0, SEEK_SET);
			forced_read(fileHandle, compressed, byteLength);
			lastSegment = typed_malloc(offset, MAX_SEGMENT_SIZE);
			int pCnt;
			decompressList(compressed, -segmentCount, &pCnt, lastSegment);
			free(compressed);

			// allocate memory for segment headers
			segmentsAllocated = 8;
			segmentHeaders = typed_malloc(PostingListSegmentHeader, segmentsAllocated);

			// make internal state consistent with on-disk data
			segmentHeaders[0].postingCount = pCnt;
			segmentHeaders[0].byteLength = byteLength;
			segmentHeaders[0].firstElement = lastSegment[0];
			segmentHeaders[0].lastElement = lastSegment[pCnt - 1];
			postingCount = pCnt;
			segmentCount = 1;
			lastSegmentIsInMemory = true;
			fileSizeWithoutHeaders = byteLength;
		}
	}
} // end of PostingListInFile(char*)


PostingListInFile::~PostingListInFile() {
	if (fileHandle < 0)
		return;

	if (modified) {
		// file has been modified: save current segment and all headers to disk

		if (postingCount < MIN_SEGMENT_SIZE) {
			// special treatment for very short lists: save some space by omitting
			// segment header
			assert(postingCount > 0);
			int byteSize;
			byte *compressed =
				compressorForID[INDEX_COMPRESSION_MODE](lastSegment, postingCount, &byteSize);
			lseek(fileHandle, 0, SEEK_SET);
			forced_write(fileHandle, compressed, byteSize);
			free(compressed);
			segmentCount = -byteSize;
			forced_write(fileHandle, &segmentCount, sizeof(segmentCount));
		}
		else {
			// normal case: write last segment, followed by segment headers
			if (lastSegmentIsInMemory)
				writeLastSegmentToDisk();
			int headerSize = segmentCount * sizeof(PostingListSegmentHeader);
			lseek(fileHandle, fileSizeWithoutHeaders, SEEK_SET);
			forced_write(fileHandle, segmentHeaders, headerSize);
			forced_write(fileHandle, &segmentCount, sizeof(segmentCount));
		}
	}

	if (fileName != NULL) {
		free(fileName);
		fileName = NULL;
	}
	if (fileHandle >= 0) {
		close(fileHandle);
		fileHandle = -1;
	}
	if (segmentHeaders != NULL) {
		free(segmentHeaders);
		segmentHeaders = NULL;
	}
	if (lastSegment != NULL) {
		free(lastSegment);
		lastSegment = NULL;
	}
} // end of ~PostingListInFile()


void PostingListInFile::addPostings(offset *postings, int count) {
	if (fileHandle < 0)
		return;

	PostingListSegmentHeader *segHeader = NULL;
	postingCount += count;
	modified = true;

	// if there are no postings in the file yet, simply create a new segment
	if (segmentCount == 0)
		goto addPostings_addSegments;

	// if the currently last segment and the new postings are too short to form
	// separate segments, load last segment into memory so that they can be merged
	segHeader = &segmentHeaders[segmentCount - 1];
	if (!lastSegmentIsInMemory) {
		if (segHeader->postingCount < MIN_SEGMENT_SIZE)
			loadLastSegmentIntoMemory();
		else if ((count < MIN_SEGMENT_SIZE) && (count + segHeader->postingCount < MAX_SEGMENT_SIZE))
			loadLastSegmentIntoMemory();
	}

	// merge currently last segment with new data if possible
	if (lastSegmentIsInMemory) {
		int spaceLeft = MAX_SEGMENT_SIZE - segHeader->postingCount;
		if (count <= spaceLeft) {
			addToLastSegment(postings, count);
			count = 0;
		}
		else if (count > spaceLeft + MIN_SEGMENT_SIZE) {
			addToLastSegment(postings, spaceLeft);
			postings = &postings[spaceLeft];
			count -= spaceLeft;
		}
		else {
			spaceLeft = TARGET_SEGMENT_SIZE - segHeader->postingCount;
			if (spaceLeft > 0) {
				addToLastSegment(postings, spaceLeft);
				postings = &postings[spaceLeft];
				count -= spaceLeft;
			}
		}
	} // end if (lastSegmentIsInMemory)

addPostings_addSegments:

	while (count > 0) {
		int chunkSize = TARGET_SEGMENT_SIZE;
		if (count <= MAX_SEGMENT_SIZE)
			chunkSize = count;
		addNewSegment(postings, chunkSize);
		postings = &postings[chunkSize];
		count -= chunkSize;
	}
} // end of addPostings(offset*, int)


void PostingListInFile::addPostings(
		byte *compressed, int byteSize, int count, offset first, offset last) {
	if (fileHandle < 0)
		return;
	modified = true;

	// if this is the first segment, then there is not much merging we can do
	if (segmentCount == 0) {
		if (count >= MIN_SEGMENT_SIZE) {
			addNewSegment(compressed, byteSize, count, first, last);
			postingCount += count;
		}
		else {
			int cnt;
			offset *uncompressed = decompressList(compressed, byteSize, &cnt, NULL);
			assert(cnt == count);
			addPostings(uncompressed, count);
			free(uncompressed);
		}
		return;
	} // end if (segmentCount == 0)

	// if the currently last segment is within the allowable bounds, and the
	// incoming segment as well, then we just copy the compressed postings
	PostingListSegmentHeader *segHeader = &segmentHeaders[segmentCount - 1];
	if (segHeader->postingCount >= MIN_SEGMENT_SIZE) {
		if ((count >= MIN_SEGMENT_SIZE) && (count <= MAX_SEGMENT_SIZE)) {
			addNewSegment(compressed, byteSize, count, first, last);
			postingCount += count;
			return;
		}
	}

	// otherwise, we have to decompress the postings and merge them with the
	// currently last segment
	int cnt;
	offset *uncompressed = decompressList(compressed, byteSize, &cnt, NULL);
	assert(cnt == count);
	addPostings(uncompressed, count);
	free(uncompressed);
} // end of addPostings(byte*, int, int, offset, offset)


void PostingListInFile::loadLastSegmentIntoMemory() {
	assert(segmentCount > 0);
	assert(!lastSegmentIsInMemory);

	// load compressed postings from disk
	PostingListSegmentHeader *segHeader = &segmentHeaders[segmentCount - 1];
	int segmentSize = segHeader->byteLength;
	off_t segmentStart = fileSizeWithoutHeaders - segmentSize;
	byte *compressed = (byte*)malloc(segmentSize);
	lseek(fileHandle, segmentStart, SEEK_SET);
	forced_read(fileHandle, compressed, segmentSize);

	// decompress postings into "lastSegment" buffer
	int cnt;
	if (lastSegment == NULL)
		lastSegment = typed_malloc(offset, MAX_SEGMENT_SIZE);
	decompressList(compressed, segHeader->byteLength, &cnt, lastSegment);
	assert(cnt == segHeader->postingCount);
	free(compressed);
	lastSegmentIsInMemory = true;
} // end of loadLastSegmentIntoMemory()


void PostingListInFile::addToLastSegment(offset *postings, int count) {
	PostingListSegmentHeader *segHeader = &segmentHeaders[segmentCount - 1];
	assert(lastSegmentIsInMemory);
	assert(segHeader->postingCount + count <= MAX_SEGMENT_SIZE);
	memcpy(&lastSegment[segHeader->postingCount], postings, count * sizeof(offset));
	segHeader->postingCount += count;
	segHeader->firstElement = lastSegment[0];
	segHeader->lastElement = postings[count - 1];
} // end of addToLastSegment(offset*, int)


void PostingListInFile::writeLastSegmentToDisk() {
	assert(lastSegmentIsInMemory);
	assert(segmentHeaders[segmentCount - 1].postingCount > 0);

	// compress postings and update meta-data
	PostingListSegmentHeader *segHeader = &segmentHeaders[segmentCount - 1];
	int byteSize;
	byte *compressed =
		compressorForID[INDEX_COMPRESSION_MODE](lastSegment, segHeader->postingCount, &byteSize);
	fileSizeWithoutHeaders += (byteSize - segHeader->byteLength);
	fileSize = fileSizeWithoutHeaders +
		segmentCount * sizeof(PostingListSegmentHeader) + sizeof(segmentCount);

	segHeader->byteLength = byteSize;
	segHeader->firstElement = lastSegment[0];
	segHeader->lastElement = lastSegment[segHeader->postingCount - 1];


	// write compressed segment to disk
	off_t startPosition = fileSizeWithoutHeaders - byteSize;
	lseek(fileHandle, startPosition, SEEK_SET);
	forced_write(fileHandle, compressed, byteSize);
	free(compressed);

	lastSegmentIsInMemory = false;
} // end of writeLastSegmentToDisk()


void PostingListInFile::addEmptySegment() {
	if (lastSegmentIsInMemory)
		writeLastSegmentToDisk();

	// make sure we have a free slot for the segment descriptor
	if (segmentCount >= segmentsAllocated) {
		segmentsAllocated = (int)(segmentsAllocated * 1.25);
		if (segmentsAllocated < segmentCount + 8)
			segmentsAllocated = segmentCount + 8;
		segmentHeaders =
			typed_realloc(PostingListSegmentHeader, segmentHeaders, segmentsAllocated);
	}

	// initialize segment header
	segmentCount++;
	PostingListSegmentHeader *segHeader = &segmentHeaders[segmentCount - 1];
	segHeader->postingCount = 0;
	segHeader->byteLength = 0;
} // end of addEmptySegment()


void PostingListInFile::addNewSegment(offset *postings, int count) {
	if ((count <= 0) || (fileHandle < 0))
		return;

	if (count < MIN_SEGMENT_SIZE) {
		addEmptySegment();
		if (lastSegment == NULL)
			lastSegment = typed_malloc(offset, TARGET_SEGMENT_SIZE);
		lastSegmentIsInMemory = true;
		addToLastSegment(postings, count);
	}
	else {
		int byteSize;
		byte *compressed =
			compressorForID[INDEX_COMPRESSION_MODE](postings, count, &byteSize);
		addNewSegment(compressed, byteSize, count, postings[0], postings[count - 1]);
		free(compressed);
	}
} // end of addNewSegment(offset*, int)


void PostingListInFile::addNewSegment(
		byte *postings, int byteSize, int count, offset first, offset last) {
	if ((count <= 0) || (fileHandle < 0))
		return;

	addEmptySegment();
	PostingListSegmentHeader *segHeader = &segmentHeaders[segmentCount - 1];
	segHeader->postingCount = count;
	segHeader->byteLength = byteSize;
	segHeader->firstElement = first;
	segHeader->lastElement = last;

	fileSizeWithoutHeaders += byteSize;
	fileSize = fileSizeWithoutHeaders +
		segmentCount * sizeof(PostingListSegmentHeader) + sizeof(segmentCount);

	lseek(fileHandle, fileSizeWithoutHeaders - byteSize, SEEK_SET);
	forced_write(fileHandle, postings, byteSize);
} // end of addNewSegment(byte*, int, int, offset, offset)


ExtentList * PostingListInFile::getPostings(int memoryLimit) {
	PostingListSegmentHeader segmentHeader;
	if ((postingCount == 0) || (fileHandle < 0))
		return new ExtentList_Empty();

	// if the list is very short, we just make it a simple PostingList object
	if ((segmentCount <= 1) && (lastSegmentIsInMemory))
		return new PostingList(lastSegment, postingCount, true, true);

	// otherwise, we have to traverse the entire sequence of segments and
	// construct a SegmentedPostingList instance
	FileFile *file = new FileFile(fileName, 0);
	SPL_OnDiskSegment *segments =
		typed_malloc(SPL_OnDiskSegment, segmentCount + 1);
	off_t filePosition = 0;
	for (int i = 0; i < segmentCount; i++) {
		segments[i].file = new FileFile(file, filePosition);
		segments[i].byteLength = segmentHeaders[i].byteLength;
		segments[i].count = segmentHeaders[i].postingCount;
		segments[i].firstPosting = segmentHeaders[i].firstElement;
		segments[i].lastPosting = segmentHeaders[i].lastElement;
		filePosition += segmentHeaders[i].byteLength;
	}
	assert(filePosition == fileSizeWithoutHeaders);
	return new SegmentedPostingList(segments, segmentCount);
} // end of getPostings()



