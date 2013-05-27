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
 * Implementation of the class StringBufferSegment. An instance of StringBufferSegment
 * is used to store strings put into an instance of StringBuffer.
 *
 * author: Stefan Buettcher
 * created: 2004-09-08
 * changed: 2004-09-28
 **/


#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "stringbuffer.h"
#include "alloc.h"


void StringBufferSegment::init(int maxLength, int maxStringCount) {
	this->maxLength = maxLength;
	this->maxStringCount = maxStringCount;
	data = (char*)malloc(maxLength);
	offset = (int16_t*)malloc(maxStringCount * sizeof(int16_t));
	for (int i = 0; i < maxStringCount; i++)
		offset[i] = -1;
	startOfFreeSpace = 0;
	stringCount = 0;
	deleteCount = 0;
	nextFreeIndex = 0;
	freeIndexCount = maxFreeIndexCount = maxStringCount / 8;
	freeIndexes = (int16_t*)malloc(maxFreeIndexCount * sizeof(int16_t));
	for (int i = 0; i < maxFreeIndexCount; i++)
		freeIndexes[i] = i;
} // end of init(int, int)


StringBufferSegment::StringBufferSegment() {
	init(DEFAULT_MAX_SEGMENT_SIZE, DEFAULT_MAX_STRING_COUNT);
} // end of StringBufferSegment()


StringBufferSegment::StringBufferSegment(int maxLength, int maxStringCount) {
	init(maxLength, maxStringCount);
} // end of StringBufferSegment(int, int)


StringBufferSegment::StringBufferSegment(File *f) {
	// read setup information from disk
	f->read(sizeof(maxStringCount), &maxStringCount);
	f->read(sizeof(maxLength), &maxLength);
	f->read(sizeof(startOfFreeSpace), &startOfFreeSpace);
	int freeSpace = startOfFreeSpace;

	// initialize and read data
	init(maxLength, maxStringCount);
	startOfFreeSpace = freeSpace;
	f->read(maxStringCount * sizeof(int16_t), offset);
	f->read(startOfFreeSpace * sizeof(char), data);

	// update management information
	computeFreeIndexes();
} // end of StringBufferSegment(File*)


StringBufferSegment::~StringBufferSegment() {
	free(data);
	free(offset);
	free(freeIndexes);
} // end of ~StringBufferSegment()


int StringBufferSegment::getLength() {
	return startOfFreeSpace;
} // end of getLength()


int StringBufferSegment::getMaxLength() {
	return maxLength;
} // end of getMaxLength()


int StringBufferSegment::getStringCount() {
	return stringCount;
} // end of getStringCount()


int StringBufferSegment::getMaxStringCount() {
	return maxStringCount;
} // end of getMaxStringCount()


int StringBufferSegment::addString(char *s) {
	int length = strlen(s);
	if ((freeIndexCount == 0) || (startOfFreeSpace + length >= maxLength))
		return -1;

	// set index and offset
	int index = freeIndexes[nextFreeIndex++];
	if (nextFreeIndex == maxFreeIndexCount)
		nextFreeIndex = 0;
	freeIndexCount--;
	offset[index] = startOfFreeSpace;
	stringCount++;

	// copy string
	strcpy(&data[startOfFreeSpace], s);
	startOfFreeSpace += length + 1;

	// check if the list of free indexes has to be refilled
	if ((freeIndexCount == 0) && (stringCount < (maxStringCount * 7) / 8))
		computeFreeIndexes();

	return index;
} // end of addString(char*)


void StringBufferSegment::computeFreeIndexes() {
	nextFreeIndex = 0;
	freeIndexCount = 0;
	stringCount = 0;
	for (int i = 0; i < maxStringCount; i++) {
		if (offset[i] >= 0)
			stringCount++;
		else if (freeIndexCount < maxStringCount / 8)
			freeIndexes[freeIndexCount++] = i;
	}
} // end of computeFreeIndexes()


char * StringBufferSegment::getString(int index) {
	assert(index < maxStringCount);
	int off = offset[index];
	if (off < 0)
		return NULL;
	else
		return &data[off];
} // end of getString(int)


void StringBufferSegment::deleteString(int index) {
	if ((index < 0) || (index >= maxStringCount))
		return;
	offset[index] = -1;
	stringCount--;
	deleteCount++;
	if ((startOfFreeSpace > (maxLength * 3) / 4) && (deleteCount >= stringCount / 5))
		compact();
	if (freeIndexCount < maxFreeIndexCount) {
		freeIndexes[(nextFreeIndex + freeIndexCount) % maxFreeIndexCount] = index;
		freeIndexCount++;
	}
} // end of deleteString(int)


void StringBufferSegment::compact() {
	int length = 0;
	char *newData = (char*)malloc(maxLength + 4);
	for (int i = 0; i < maxStringCount; i++)
		if (offset[i] >= 0) {
			int oldOffset = offset[i];
			int newOffset = length;
			offset[i] = newOffset;
			while (data[oldOffset] != 0)
				newData[newOffset++] = data[oldOffset++];
			newData[newOffset++] = 0;
			length = newOffset;
		}
	free(data);
	data = newData;
	startOfFreeSpace = length;
	deleteCount = 0;
} // end of compact()


int StringBufferSegment::saveToFile(File *f) {
#define STF_WRITE(size, ptr) \
	if (f->write(size, ptr) == FILESYSTEM_ERROR) \
		return FILESYSTEM_ERROR

	// compact and write all information to disk
	if (deleteCount > 0)
		compact();
	STF_WRITE(sizeof(maxStringCount), &maxStringCount);
	STF_WRITE(sizeof(maxLength), &maxLength);
	STF_WRITE(sizeof(startOfFreeSpace), &startOfFreeSpace);
	STF_WRITE(maxStringCount * sizeof(int16_t), offset);
	STF_WRITE(startOfFreeSpace * sizeof(char), data);
	return FILESYSTEM_SUCCESS;

#undef STF_WRITE
} // end of saveToFile(File*)


bool StringBufferSegment::canAdd(int length) {
	if ((freeIndexCount > 0) && (startOfFreeSpace + length < maxLength))
		return true;
	else
		return false;
} // end of canAdd()


int StringBufferSegment::maxInsertLength() {
	if (freeIndexCount > 0)
		return maxLength - startOfFreeSpace - 1;
	else
		return 0;
} // end of maxInsertLength()


