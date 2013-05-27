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
 * Implementation of the class File.
 * Documentation can be found in file "filesystem.h".
 *
 * author: Stefan Buettcher
 * created: 2004-09-03
 * changed: 2006-08-28
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesystem.h"
#include "../misc/all.h"


// For every file, we hold an array of all pages numbers in memory. This
// value is the minimum size of this array.
#define MINIMUM_PAGES_ARRAY_SIZE 16


void File::init(FileSystem *fileSystem, fs_fileno fileHandle, bool create) {
	this->pages = NULL;
	this->handle = -1;
	assert(fileSystem->isActive());
	this->fileSystem = fileSystem;
	if (create)
		fileHandle = fileSystem->createFile(fileHandle);
	if (fileHandle < 0) {
		fprintf(stderr, "Negative file handle!\n");
		return;
	}

	// obtain first page of the file
	firstPage = fileSystem->getFirstPage(fileHandle);
	if (firstPage < 0)
		printAllocations();
	assert(firstPage >= 0);

	handle = fileHandle;
	seekPos = 0;
	pageSize = fileSystem->getPageSize();
	pageCount = fileSystem->getPageCount(fileHandle);
	if (pageCount <= 1)
		allocatedCount = 0;
	else if (pageCount <= MINIMUM_PAGES_ARRAY_SIZE / 2)
		allocatedCount = MINIMUM_PAGES_ARRAY_SIZE;
	else
		allocatedCount = pageCount * 2;
	if (allocatedCount == 0)
		pages = &firstPage;
	else
		pages = typed_malloc(int32_t, allocatedCount);

	// read all page numbers into memory
	int32_t page = firstPage;
	int lc = 0;
	do {
		pages[lc++] = page;
		page = fileSystem->getPageStatus(page);
	} while (page > 0);
	assert(lc == pageCount);
	int32_t lengthOfLastPage = -page;
	size = (pageCount - 1) * pageSize + lengthOfLastPage;
} // end of init(FileSystem*, fs_fileno, bool)


File::File(FileSystem *fileSystem) {
	init(fileSystem, -1, true);
} // end of File(FileSystem*)


File::File(FileSystem *fileSystem, fs_fileno fileHandle, bool create) {
	init(fileSystem, fileHandle, create);
} // end of File(FileSystem*, fs_fileno, bool)


File::File() {
	pages = NULL;
}


File::~File() {
	if (pages != &firstPage) {
		if (pages != NULL)
			free(pages);
	}
	pages = NULL;
} // end of ~File()


void File::deleteFile() {
	if (handle >= 0)
		fileSystem->deleteFile(handle);
	handle = -1;
} // end of deleteFile()


fs_fileno File::getHandle() {
	return handle;
} // end of getHandle()


off_t File::getSize() {
	return size;
} // end of getSize()


int32_t File::getPageCount() {
	return pageCount;
}


off_t File::getSeekPos() {
	return seekPos;
}


int File::seek(off_t newSeekPos) {
	LocalLock lock(this);
	if ((newSeekPos < 0) || (newSeekPos > size))
		return FILESYSTEM_ERROR;
	seekPos = newSeekPos;
	return FILESYSTEM_SUCCESS;
} // end of seek(int32_t)


int File::read(int bufferSize, void *buffer) {
	LocalLock lock(this);
	char *data = (char*)buffer;
	int readCount = 0;
	if (seekPos >= size)
		return 0;
	while (bufferSize > 0) {
		int32_t page = pages[seekPos / pageSize];
		int32_t pageOffset = seekPos % pageSize;
		int32_t toRead = pageSize - pageOffset;
		if (toRead > bufferSize)
			toRead = bufferSize;
		if (toRead > size - seekPos)
			toRead = size - seekPos;
		if (toRead == 0)
			break;

		// read data from filesystem
		int result = fileSystem->readPage(page, pageOffset, toRead, data);
		if (result < 0)
			return FILESYSTEM_ERROR;

		// update variables
		readCount += toRead;
		bufferSize -= toRead;
		data += toRead;
		seekPos += toRead;
	}
	return readCount;
} // end of read(int, void*)


int File::seekAndRead(off_t position, int bufferSize, void *buffer) {
	LocalLock lock(this);
	seek(position);
	return read(bufferSize, buffer);
} // end of seekAndRead(off_t, int, void*)


int File::write(int bufferSize, void *buffer) {
	LocalLock lock(this);
	char *data = (char*)buffer;
	int writeCount = 0;

	while (bufferSize > 0) {
		int32_t pageNumber = seekPos / pageSize;
		int32_t mySeekPos = seekPos;

		// file is full: acquire new page to add to file
		if (pageNumber >= pageCount) {
			// try to get a new page that is close to the last page of the file
			if ((pages == &firstPage) || (pages == NULL)) {
				pages = typed_malloc(int32_t, MINIMUM_PAGES_ARRAY_SIZE);
				allocatedCount = MINIMUM_PAGES_ARRAY_SIZE;
				pages[0] = firstPage;
			}
			fs_pageno newPage = fileSystem->claimFreePage(handle, pages[pageCount - 1]);
			assert(newPage >= 0);
			if (newPage < 0)
				return FILESYSTEM_ERROR;
			pages[pageCount] = newPage;
			fileSystem->setPageStatus(pages[pageCount - 1], newPage);
			fileSystem->setPageStatus(newPage, 0);
			fileSystem->setPageCount(handle, ++pageCount);

			// if "pages" array is full, allocate new memory
			if (pageCount >= allocatedCount) {
				allocatedCount *= 2;
				pages = (int32_t*)realloc(pages, allocatedCount * sizeof(int32_t));
				assert(pages != NULL);
			}
		} // end if (pageNumber >= pageCount)

		seekPos = mySeekPos;

		int32_t page = pages[pageNumber];
		int32_t pageOffset = seekPos % pageSize;
		int32_t toWrite = pageSize - pageOffset;
		if (toWrite > bufferSize)
			toWrite = bufferSize;

		// write data to filesystem
		int result = fileSystem->writePage(page, pageOffset, toWrite, data);
		if (result < 0)
			return FILESYSTEM_ERROR;

		// update variables
		writeCount += toWrite;
		bufferSize -= toWrite;
		data += toWrite;
		seekPos += toWrite;

		// update file size if necessary
		if (seekPos > size) {
			size = seekPos;
			if ((size % pageSize) == 0)
				fileSystem->setPageStatus(pages[pageCount - 1], -pageSize);
			else
				fileSystem->setPageStatus(pages[pageCount - 1], -(size % pageSize));
		} // end if (seekPos > size)
	}

	return writeCount;
} // end of write(int, void*)


void * File::read(int *bufferSize, bool *mustFreeBuffer) {
	void *result = malloc(*bufferSize + 1);
	*bufferSize = read(*bufferSize, result);
	*mustFreeBuffer = true;
	return result;
} // end of read(int*, bool*)


