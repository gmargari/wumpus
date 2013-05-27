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
 * Implementation of the class StringBuffer. StringBuffer instances can be
 * used to store a large number of strings, deleting some, adding new, ...
 * without having to keep track of pointers. All strings inside a StringBuffer
 * are referenced by their index, which is assigned at the time they are
 * inserted into the buffer.
 * 
 * author: Stefan Buettcher
 * created: 2004-09-12
 * changed: 2004-09-29
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stringbuffer.h"
#include "alloc.h"


StringBuffer::StringBuffer(int usualStringLength, File *f) {
	this->file = f;
	bool createNew = false;
	if (f == NULL)
		createNew = true;
	else if (f->getSize() == 0)
		createNew = true;

	if (createNew) {
		// initialize variables etc.
		this->usualStringLength = usualStringLength;
		segmentCount = 1;
		segment = (StringBufferSegment**)malloc(segmentCount * sizeof(StringBufferSegment*));
		segment[0] = new StringBufferSegment(MAX_STRING_SEGMENT_SIZE, MAX_STRINGS_PER_SEGMENT);
		stringCount = 0;
		deleteCount = 0;
		freeSegments = (int*)malloc(segmentCount * sizeof(int));
		segmentIsFree = (bool*)malloc(segmentCount * sizeof(bool));
		freeSegments[0] = 0;
		segmentIsFree[0] = true;
		nextFreeSegment = 0;
		freeSegmentCount = segmentCount;
	}
	else {
		// read general setup information
		f->seek(0);
		f->read(sizeof(segmentCount), &segmentCount);
		f->read(sizeof(this->usualStringLength), &this->usualStringLength);

		// initialize segments and update "free segments" information
		segment = (StringBufferSegment**)malloc(segmentCount * sizeof(StringBufferSegment*));
		freeSegments = (int*)malloc(segmentCount * sizeof(int));
		segmentIsFree = (bool*)malloc(segmentCount * sizeof(bool));
		stringCount = 0; deleteCount = 0;
		nextFreeSegment = freeSegmentCount = 0;
		for (int i = 0; i < segmentCount; i++) {
			segment[i] = new StringBufferSegment(f);
			stringCount += segment[i]->getStringCount();
			if (segment[i]->maxInsertLength() >= usualStringLength) {
				segmentIsFree[i] = true;
				freeSegments[freeSegmentCount++] = i;
			}
			else
				segmentIsFree[i] = false;
		}
	} // end else

} // end of StringBuffer(File*)


StringBuffer::~StringBuffer() {
	saveToFile();
	if (file != NULL)
		delete file;
	for (int i = 0; i < segmentCount; i++)
		delete segment[i];
	free(segment);
	free(freeSegments);
	free(segmentIsFree);
} // end of ~StringBuffer()


int StringBuffer::saveToFile() {
	if (file == NULL)
		return FILESYSTEM_SUCCESS;
	file->seek(0);

	// first, write general information about the StringBuffer
	if (file->write(sizeof(segmentCount), &segmentCount) == FILESYSTEM_ERROR)
		return FILESYSTEM_ERROR;
	if (file->write(sizeof(usualStringLength), &usualStringLength) == FILESYSTEM_ERROR)
		return FILESYSTEM_ERROR;

	// then, write the segment data
	for (int i = 0; i < segmentCount; i++)
		if (segment[i]->saveToFile(file) == FILESYSTEM_ERROR)
			return FILESYSTEM_ERROR;
	return FILESYSTEM_SUCCESS;
} // end of saveToFile(File*)


int StringBuffer::addString(char *s) {

	stringCount++;

	if (freeSegmentCount > 0) {
		// we know there is a free segment: put the string into that segment
		int seg = freeSegments[nextFreeSegment];
		int result = segment[seg]->addString(s);

		if (result >= 0) {
			// string could be added to the segment: continue
			if (segment[seg]->maxInsertLength() < usualStringLength) {
				segmentIsFree[seg] = false;
				freeSegmentCount--;
				nextFreeSegment++;
				if (nextFreeSegment == segmentCount)
					nextFreeSegment = 0;
			}
			return seg * MAX_STRINGS_PER_SEGMENT + result;
		}
	} // end if (freeSegmentCount > 0)

	// there are currently no free segments: create a new segment
	StringBufferSegment **newSegment =
		(StringBufferSegment**)malloc((segmentCount + 1) * sizeof(StringBufferSegment*));
	for (int i = 0; i < segmentCount; i++)
		newSegment[i] = segment[i];
	newSegment[segmentCount++] =
		new StringBufferSegment(MAX_STRING_SEGMENT_SIZE, MAX_STRINGS_PER_SEGMENT);
	free(segment);
	segment = newSegment;

	// update the "segment is free" information
	free(freeSegments); freeSegments = (int*)malloc(segmentCount * sizeof(int));
	free(segmentIsFree); segmentIsFree = (bool*)malloc(segmentCount * sizeof(bool));
	nextFreeSegment = 0;
	freeSegmentCount = 0;
	for (int i = 0; i < segmentCount; i++) {
		if (segment[i]->maxInsertLength() >= usualStringLength) {
			segmentIsFree[i] = true;
			freeSegments[freeSegmentCount++] = i;
		}
		else
			segmentIsFree[i] = false;
	}

	// add string to the newly created segment
	return MAX_STRINGS_PER_SEGMENT * (segmentCount - 1) +
		segment[segmentCount - 1]->addString(s);
} // end of addString(char*)


char * StringBuffer::getString(int index) {
	// getString is the method that is probably called the most. We want it to
	// be as fast as possible. Therefore, we are directly accessing the internal
	// structures of the segment holding the string.
	int segmentNumber = index / MAX_STRINGS_PER_SEGMENT;
	int indexInSegment = index % MAX_STRINGS_PER_SEGMENT;
	assert(segmentNumber < segmentCount);
	int offset = segment[segmentNumber]->offset[indexInSegment];
	return &segment[segmentNumber]->data[offset];
} // end of getString(int)


void StringBuffer::deleteString(int index) {
	int segmentNumber = index / MAX_STRINGS_PER_SEGMENT;
	int indexInSegment = index % MAX_STRINGS_PER_SEGMENT;
	assert(segmentNumber < segmentCount);
	segment[segmentNumber]->deleteString(indexInSegment);

	stringCount--;
	deleteCount++;

	// if necessary, update the "free storage space" information
	if (!segmentIsFree[segmentNumber]) {
		if (segment[segmentNumber]->maxInsertLength() >= usualStringLength) {
			segmentIsFree[segmentNumber] = true;
			freeSegments[(nextFreeSegment + freeSegmentCount) % segmentCount] = segmentNumber;
			freeSegmentCount++;
		}
	}
} // end of deleteString(int)


int StringBuffer::getSegmentCount() {
	return segmentCount;
} // end of getSegmentCount()


int StringBuffer::getStringCount() {
	return stringCount;
} // end of getStringCount()


void StringBuffer::compact() {
	for (int i = 0; i < segmentCount; i++)
		segment[i]->compact();
} // end of compact()


// insert/remove "//" as you wish
//#define STRINGBUFFER_DEBUG

#ifdef STRINGBUFFER_DEBUG

int main() {
	StringBuffer *sb = new StringBuffer(32);
	int index[100000];
	char text[256];
	int numberOfStrings = 100000;
	for (int i = 0; i < numberOfStrings; i++) {
		sprintf(text, "String %i", i);
		index[i] = sb->addString(text);
	}
	for (int i = 0; i < numberOfStrings; i++) {
		sprintf(text, "String %i", i);
		if (strcmp(text, sb->getString(index[i])) != 0) {
			fprintf(stderr, "Error at index %i: %s != %s\n", i, text, sb->getString(index[i]));
			return 1;
		}
	}

	printf("Number of segments after %i insertions: %i (%i strings stored).\n",
			numberOfStrings, sb->getSegmentCount(), sb->getStringCount());

	FileSystem *fs = new FileSystem("../temp/data_file", 4096, 4096);
	File *f = new File(fs, 0, true);
	sb->saveToFile(f);
	delete f;
	delete sb;

	for (int lc = 0; lc < 8; lc++) {

		f = new File(fs, 0, false);
		sb = new StringBuffer(f);
		delete f;

		printf("Number of strings stored: %i.\n", sb->getStringCount());

		int deleteCount = 0;
		int maxNumber = StringBuffer::MAX_STRINGS_PER_SEGMENT * sb->getSegmentCount();
		for (int i = 0; i < numberOfStrings / 4; i++) {
			int number = random() % maxNumber;
			if (sb->getString(number) != NULL) {
				deleteCount++;
				sb->deleteString(number);
			}
		}

		printf("Number of segments after %i deletions: %i (%i strings stored).\n",
				deleteCount, sb->getSegmentCount(), sb->getStringCount());

		for (int i = 0; i < deleteCount; i++) {
			sprintf(text, "String %i", i);
			int length = random() % 25 + 5;
			for (int j = 0; j < length; j++)
				text[j] = (char)('a' + random() % 26);
			text[length] = 0;
			index[i] = sb->addString(text);
			if (sb->getString(index[i]) == NULL) {
				fprintf(stderr, "Problem: %i/%i, %s\n", i, index[i], text);
				return 1;
			}
			if (strcmp(text, sb->getString(index[i])) != 0) {
				fprintf(stderr, "Error at index %i: %s != %s\n", i, text, sb->getString(index[i]));
				return 1;
			}
		}

		printf("Number of segments after %i insertions: %i (%i strings stored).\n",
				deleteCount, sb->getSegmentCount(), sb->getStringCount());

    fs->deleteFile(0);
		f = new File(fs, 0, true);
		sb->saveToFile(f);
		delete f;
		delete sb;

	} // end for (int lc = 0; lc < 10; lc++)
		
	return 0;
} // end of main()

#endif




