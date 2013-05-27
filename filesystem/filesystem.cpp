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
 * Implementation of the class FileSystem.
 * Documentation can be found in file "filesystem.h".
 *
 * author: Stefan Buettcher
 * created: 2004-09-03
 * changed: 2006-08-28
 **/


#include "filesystem.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../index/index_types.h"
#include "../misc/alloc.h"
#include "../misc/assert.h"
#include "../misc/io.h"


FileSystem::FileSystem(char *fileName) {

	cache = NULL;
	dataFileName = (char*)malloc(strlen(fileName) + 2);
	strcpy(dataFileName, fileName);

	// open file, set internal variables
	dataFile = open(fileName, FILESYSTEM_ACCESS);
	if (dataFile < 0) {
		fprintf(stderr, "Filesystem \"%s\" could not be opened.\n", fileName);
		perror(NULL);
		return;
	}

	// read preamble from disk
	int32_t *pageBuffer = (int32_t*)malloc(512 + INT_SIZE);
	lseek(dataFile, 0, SEEK_SET);
	if (forced_read(dataFile, pageBuffer, 512) != 512) {
		close(dataFile);
		dataFile = -1;
		fprintf(stderr, "Could not read preamble from filesystem \"%s\".\n", fileName);
		return;
	}
	int32_t fingerprintOnDisk = pageBuffer[0];
	int32_t pageSizeOnDisk = pageBuffer[1];
	int32_t pageCountOnDisk = pageBuffer[2];
	int32_t pageLayoutSizeOnDisk = pageBuffer[3];
	int32_t fileMappingSizeOnDisk = pageBuffer[4];
	cacheSize = pageBuffer[5];
	free(pageBuffer);
	
	pageSize = pageSizeOnDisk;
	intsPerPage = pageSize / INT_SIZE;
	doubleIntsPerPage = intsPerPage / 2;
	pageCount = pageCountOnDisk;
	pageLayoutSize = pageLayoutSizeOnDisk;
	fileMappingSize = fileMappingSizeOnDisk;

	// check if we have valid data
	if ((fingerprintOnDisk != FINGERPRINT) ||
			(pageSize < MIN_PAGE_SIZE) || (pageCount < MIN_PAGE_COUNT)) {
		close(dataFile);
		dataFile = -1;
		return;
	}

	// the first page contains PREAMBLE_LENGTH bytes and is not owned by a file
	if (getPageStatus(0) != -PREAMBLE_LENGTH) {
		close(dataFile);
		dataFile = -1;
		return;
	}

	cachedReadCnt = cachedWriteCnt = 0;
	uncachedReadCnt = uncachedWriteCnt = 0;

	freePages = freeFileNumbers = NULL;
	initializeFreeSpaceArrays();
	enableCaching();
} // end of FileSystem(char*)


FileSystem::FileSystem(char *fileName, int pageSize, int pageCount) {
	init(fileName, pageSize, pageCount, DEFAULT_CACHE_SIZE);
}


FileSystem::FileSystem(char *fileName, int pageSize, fs_pageno pageCount, int cacheSize) {
	init(fileName, pageSize, pageCount, cacheSize);
}


void FileSystem::init(char *fileName, int pageSize, fs_pageno pageCount, int cacheSize) {

	cache = NULL;
	this->cacheSize = cacheSize;
	dataFileName = (char*)malloc(strlen(fileName) + 2);
	strcpy(dataFileName, fileName);

	// check if the argument values are allowed
	if ((pageCount < MIN_PAGE_COUNT) || (pageSize < MIN_PAGE_SIZE)) {
		fprintf(stderr, "Illegal pageCount/pageSize values: %i/%i\n", (int)pageCount, (int)pageSize);
		dataFile = -1;
		return;
	}
	if ((pageCount > MAX_PAGE_COUNT) || (pageSize > MAX_PAGE_SIZE)) {
		fprintf(stderr, "Illegal pageCount/pageSize values: %i/%i\n", (int)pageCount, (int)pageSize);
		dataFile = -1;
		return;
	}
	if ((pageSize % INT_SIZE != 0) || (pageCount % (pageSize / INT_SIZE) != 0)) {
		fprintf(stderr, "Illegal pageCount/pageSize values: %i/%i\n", (int)pageCount, (int)pageSize);
		dataFile = -1;
		return;
	}

	// open file, set internal variables
	dataFile = open(fileName, O_CREAT | O_TRUNC | FILESYSTEM_ACCESS, DEFAULT_FILE_PERMISSIONS);
	if (dataFile < 0) {
		fprintf(stderr, "Could not create filesystem \"%s\".\n", fileName);
		perror(NULL);
		return;
	}
	this->pageSize = pageSize;
	intsPerPage = pageSize / INT_SIZE;
	doubleIntsPerPage = intsPerPage / 2;
	this->pageCount = pageCount;
	this->pageLayoutSize = (pageCount + (intsPerPage - 1)) / intsPerPage;
	this->fileMappingSize = 1;

	off_t fileSize = pageSize;
	fileSize *= (pageCount + pageLayoutSize + fileMappingSize);
	if (ftruncate(dataFile, fileSize) < 0) {
		fprintf(stderr, "Could not set filesystem size.\n");
		perror(NULL);
		close(dataFile);
		dataFile = -1;
		return;
	}
	if (getSize() != fileSize) {
		fprintf(stderr, "Could not set filesystem size.\n");
		perror(NULL);
		close(dataFile);
		dataFile = -1;
		return;
	}
	
	// write preamble to disk
	int32_t fingerprintOnDisk = FINGERPRINT;
	int32_t pageSizeOnDisk = (int32_t)pageSize;
	int32_t pageCountOnDisk = (int32_t)pageCount;
	int32_t pageLayoutSizeOnDisk = (int32_t)pageLayoutSize;
	int32_t fileMappingSizeOnDisk = (int32_t)fileMappingSize;
	lseek(dataFile, 0, SEEK_SET);
	forced_write(dataFile, &fingerprintOnDisk, INT_SIZE);
	forced_write(dataFile, &pageSizeOnDisk, INT_SIZE);
	forced_write(dataFile, &pageCountOnDisk, INT_SIZE);
	forced_write(dataFile, &pageLayoutSizeOnDisk, INT_SIZE);
	forced_write(dataFile, &fileMappingSizeOnDisk, INT_SIZE);
	forced_write(dataFile, &this->cacheSize, INT_SIZE);

	freePages = freeFileNumbers = NULL;

	// initialize page layout table
	int32_t *pageData = (int32_t*)malloc((intsPerPage + 1) * sizeof(int32_t));
	for (int i = 0; i < intsPerPage; i++)
		pageData[i] = UNUSED_PAGE;
	for (int i = 0; i < pageLayoutSize; i++)
		writePage(pageCount + i, pageData);
	// set first page to status "occupied with PREAMBLE_LENGTH bytes"
	setPageStatus(0, -PREAMBLE_LENGTH);
	// set all files to "unused"
	for (int i = 0; i < fileMappingSize * doubleIntsPerPage; i++)
		setFirstPage(i, UNUSED_PAGE);
	free(pageData);

	cachedReadCnt = cachedWriteCnt = 0;
	uncachedReadCnt = uncachedWriteCnt = 0;

	initializeFreeSpaceArrays();
	enableCaching();
} // end of FileSystem(char*, int, int)


void FileSystem::initializeFreeSpaceArrays() {
	bool mustReleaseLock = getLock();

	if (freePages == NULL) {
		freePages = (int16_t*)malloc(pageLayoutSize * sizeof(int16_t));
		for (int j = 0; j < pageLayoutSize; j++) {
			freePages[j] = 0;
			for (int k = 0; k < intsPerPage; k++)
				if (getPageStatus(j * intsPerPage + k) == UNUSED_PAGE)
					freePages[j]++;
		} // end for (int j = 0; j < pageLayoutSize; j++)
	}

	if (freeFileNumbers == NULL) {
		freeFileNumbers = (int16_t*)malloc(fileMappingSize * sizeof(int16_t));
		for (int j = 0; j < fileMappingSize; j++) {
			freeFileNumbers[j] = false;
			for (int k = 0; k < doubleIntsPerPage; k++)
				if (getFirstPage(j * doubleIntsPerPage + k) == UNUSED_PAGE)
					freeFileNumbers[j]++;
		} // end for (int j = 0; j < pageLayoutSize; j++)
	}

	if (mustReleaseLock)
		releaseLock();
} // end of initializeFreeSpaceArrays()


FileSystem::~FileSystem() {
	disableCaching();
	if (dataFile >= 0) {
		// close data file
		close(dataFile);
		dataFile = -1;
	}
	if (freePages != NULL) {
		free(freePages);
		freePages = NULL;
	}
	if (freeFileNumbers != NULL) {
		free(freeFileNumbers);
		freeFileNumbers = NULL;
	}
	if (dataFileName != NULL) {
		free(dataFileName);
		dataFileName = NULL;
	}
} // end of ~FileSystem()


void FileSystem::flushCache() {
	if (cache == NULL)
		return;
	bool mustReleaseLock = getLock();
	disableCaching();
	enableCaching();
	if (mustReleaseLock)
		releaseLock();
} // end of flushCache()


void FileSystem::enableCaching() {
	if (cache != NULL)
		return;
	bool mustReleaseLock = getLock();
	cache = new FileSystemCache(this, pageSize, cacheSize / pageSize);
	if (mustReleaseLock)
		releaseLock();
} // end of enableCaching()


void FileSystem::disableCaching() {
	if (cache == NULL)
		return;
	bool mustReleaseLock = getLock();
	delete cache;
	cache = NULL;
	if (mustReleaseLock)
		releaseLock();
} // end of disableCaching()


bool FileSystem::isActive() {
	return (dataFile >= 0);
} // end of isActive()


int FileSystem::defrag() {
	printf("defrag called\n");
	exit(1);
	int32_t nextFreePage = 1;
	int32_t *newPosition = (int32_t*)malloc(pageCount * sizeof(int32_t));
	// page 0 must not be moved around
	newPosition[0] = 0;
	for (int i = 1; i < pageCount; i++)
		newPosition[i] = -1;

	// first, perform a DFS to assign new page positions
	int upperFileLimit = doubleIntsPerPage * fileMappingSize;
	for (int file = 0; file < upperFileLimit; file++) {
		int32_t page = getFirstPage(file);
		while (page > 0) {
			assert(newPosition[page] < 0);
			newPosition[page] = nextFreePage++;
			page = getPageStatus(page);
		}
	} // end for (int file = 0; file < upperFileLimit; file++)

	// then, assign new page numbers to the remaining (free) pages
	for (int i = 1; i < pageCount; i++)
		if (getPageStatus(i) == UNUSED_PAGE)
			newPosition[i] = nextFreePage++;

	// make sure that every page has been assigned a new location
	assert(nextFreePage == pageCount);

	// correct data in the page layout table
	int32_t *oldPageLayout = (int32_t*)malloc(pageLayoutSize * intsPerPage * sizeof(int32_t));
	int32_t *newPageLayout = (int32_t*)malloc(pageLayoutSize * intsPerPage * sizeof(int32_t));
	for (int i = 0; i < pageLayoutSize; i++)
		readPage(pageCount + i, &oldPageLayout[intsPerPage * i]);
	for (int page = 0; page < pageCount; page++) {
		if (oldPageLayout[page] <= 0)
			newPageLayout[newPosition[page]] = oldPageLayout[page];
		else
			newPageLayout[newPosition[page]] = newPosition[oldPageLayout[page]];
	}
	for (int i = 0; i < pageLayoutSize; i++)
		writePage(pageCount + i, &newPageLayout[intsPerPage * i]);	
	free(newPageLayout);
	free(oldPageLayout);

	// correct page numbers in the file->page table
	for (int file = 0; file < upperFileLimit; file++) {
		int32_t page = getFirstPage(file);
		if (page >= 0)
			setFirstPage(file, newPosition[page]);
	} // end for (int file = 0; file < upperFileLimit; file++)

	byte *buffer1 = (byte*)malloc(pageSize);
	byte *buffer2 = (byte*)malloc(pageSize);
	// happy reading/writing while moving all pages to their new positions
	for (int page = 0; page < pageCount; page++) {
		int currentPage = page;
		while (newPosition[currentPage] != currentPage) {
			assert(newPosition[currentPage] >= page);
			int32_t newPos = newPosition[currentPage];

			// swap data in pages "currentPage" and "newPos"
			if (readPage(currentPage, buffer1) == FILESYSTEM_ERROR)
				return FILESYSTEM_ERROR;
			if (readPage(newPos, buffer2) == FILESYSTEM_ERROR)
				return FILESYSTEM_ERROR;
			if (writePage(newPos, buffer1) == FILESYSTEM_ERROR)
				return FILESYSTEM_ERROR;
			if (writePage(currentPage, buffer2) == FILESYSTEM_ERROR)
				return FILESYSTEM_ERROR;

			// update permutation table
			newPosition[currentPage] = newPosition[newPos];
			newPosition[newPos] = newPos;
		}
	} // end for (int page = 0; page < pageCount; page++)
	free(buffer2);
	free(buffer1);

	free(newPosition);
	return FILESYSTEM_SUCCESS;
} // end of defrag()


int FileSystem::changeSize(fs_pageno newPageCount) {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	// if "newPageCount" is too small, an error is returned
	if ((newPageCount < MIN_PAGE_COUNT) || (newPageCount < getUsedPageCount()) ||
			(newPageCount > MAX_PAGE_COUNT))
		goto endOfChangeSize;

	if (newPageCount < pageCount) {
		// First, defragment the filesystem in order to be able to reduce the size.
		if (defrag() == FILESYSTEM_ERROR)
			goto endOfChangeSize;

		// Then, decrease the size of the filesystem.
		int newPageLayoutSize = (newPageCount + (intsPerPage - 1)) / intsPerPage;

		byte *pageBuffer = (byte*)malloc(pageSize);
		
		// copy page layout data to new position
		for (int i = 0; i < newPageLayoutSize; i++) {
			readPage(pageCount + i, pageBuffer);
			writePage(newPageCount + i, pageBuffer);
		}
		
		// copy file->page mappings to new position
		for (int i = 0; i < fileMappingSize; i++) {
			readPage(pageCount + pageLayoutSize + i, pageBuffer);
			writePage(newPageCount + newPageLayoutSize + i, pageBuffer);
		}

		free(pageBuffer);

		pageCount = newPageCount;
		pageLayoutSize = newPageLayoutSize;

		// write changed preamble to disk
		int32_t pageCountOnDisk = (int32_t)pageCount;
		int32_t pageLayoutSizeOnDisk = (int32_t)pageLayoutSize;
		lseek(dataFile, 2 * INT_SIZE, SEEK_SET);
		forced_write(dataFile, &pageCountOnDisk, INT_SIZE);
		forced_write(dataFile, &pageLayoutSizeOnDisk, INT_SIZE);

		// change the size of the data file
		off_t fileSize = pageSize;
		fileSize *= (newPageCount + newPageLayoutSize + fileMappingSize);
		forced_ftruncate(dataFile, fileSize);
	} // end if (newPageCount < pageCount)

	if (newPageCount > pageCount) {
		// change the size of the data file
		int newPageLayoutSize = (newPageCount + (intsPerPage - 1)) / intsPerPage;
		off_t fileSize = pageSize;
		fileSize *= (newPageCount + newPageLayoutSize + fileMappingSize);
		if (ftruncate(dataFile, fileSize) < 0) {
			fprintf(stderr, "Filesystem size could not be changed.\n");
			perror(NULL);
			goto endOfChangeSize;
		}
		if (getSize() != fileSize) {
			fprintf(stderr, "Filesystem size could not be changed.\n");
			perror(NULL);
			goto endOfChangeSize;
		}

		int oldPageCount = pageCount;
		pageCount = newPageCount;
		int oldPageLayoutSize = pageLayoutSize;
		pageLayoutSize = newPageLayoutSize;

		// write changed preamble to disk
		int32_t pageCountOnDisk = (int32_t)pageCount;
		int32_t pageLayoutSizeOnDisk = (int32_t)pageLayoutSize;
		lseek(dataFile, 2 * INT_SIZE, SEEK_SET);
		forced_write(dataFile, &pageCountOnDisk, INT_SIZE);
		forced_write(dataFile, &pageLayoutSizeOnDisk, INT_SIZE);

		byte *pageBuffer = (byte*)malloc(pageSize);

		// copy file->page mappings to new position
		for (int i = fileMappingSize - 1; i >= 0; i--) {
			readPage(oldPageCount + oldPageLayoutSize + i, pageBuffer);
			writePage(newPageCount + newPageLayoutSize + i, pageBuffer);
		}

		// copy page layout data to new position
		for (int i = oldPageLayoutSize - 1; i >= 0; i--) {
			readPage(oldPageCount + i, pageBuffer);
			writePage(newPageCount + i, pageBuffer);
		}
		// initialize layout data for the new pages
		int32_t unusedValue = UNUSED_PAGE;
		for (int i = 0; i < intsPerPage; i++)
			memcpy(&pageBuffer[i * INT_SIZE], &unusedValue, INT_SIZE);
		for (int i = oldPageLayoutSize; i < newPageLayoutSize; i++)
			 writePage(newPageCount + i, pageBuffer);

		free(pageBuffer);
		
		// update the freePages and freeFileNumbers arrays
		free(freePages); freePages = NULL;
		free(freeFileNumbers); freeFileNumbers = NULL;
		initializeFreeSpaceArrays();
	} // end if (newPageCount > pageCount)

	enableCaching();
	result = FILESYSTEM_SUCCESS;

endOfChangeSize:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of changeSize(fs_pageno)


int FileSystem::deleteFile(fs_fileno fileHandle) {
	bool mustReleaseLock = getLock();

	fs_pageno firstPage = getFirstPage(fileHandle);
	assert(firstPage >= 0);

	// remove the file from the file->page mappings
	setFirstPage(fileHandle, UNUSED_PAGE);
	setPageCount(fileHandle, UNUSED_PAGE);

	// mark all pages occupied by the file as free
	fs_pageno page = firstPage;
	while (page > 0) {
		fs_pageno nextPage = getPageStatus(page);
		setPageStatus(page, UNUSED_PAGE);
		page = nextPage;
	}

	// check if it is appropriate to reduce the size of the file->page table
	if ((freeFileNumbers[fileMappingSize - 1] == doubleIntsPerPage) &&
			(freeFileNumbers[fileMappingSize - 2] == doubleIntsPerPage))
		decreaseFileMappingSize();

	if (mustReleaseLock)
		releaseLock();
	return FILESYSTEM_SUCCESS;
} // end of deleteFile(fs_fileno)


fs_pageno FileSystem::claimFreePage(fs_fileno owner, fs_pageno closeTo) {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	fs_pageno oldPageCount, newPageCount;

	int origCloseTo = closeTo;
	if ((closeTo < 0) || (closeTo >= pageCount)) {
		origCloseTo = 0;
		closeTo = 0;
	}
	else
		closeTo = closeTo / intsPerPage;

	// buffer for speeding up the individual status requests
	int32_t *data = (int32_t*)malloc((intsPerPage + 1) * sizeof(int32_t));
	
	if (freePages[closeTo] > 0) {
		if (readPage(pageCount + closeTo, data) == FILESYSTEM_ERROR) {
			free(data);
			goto endOfClaimFreePage;
		}
		for (int j = origCloseTo % intsPerPage; j < intsPerPage; j++)
			if (data[j] == UNUSED_PAGE) {
				free(data);
				result = closeTo * intsPerPage + j;
				goto endOfClaimFreePage;
			}
		for (int j = origCloseTo % intsPerPage; j >= 0; j--)
			if (data[j] == UNUSED_PAGE) {
				free(data);
				result = closeTo * intsPerPage + j;
				goto endOfClaimFreePage;
			}
	} // end if (freePages[closeTo] > 0)

	// if nothing close to "closeTo" can be found, just search everywhere
	for (int j = closeTo + 1; j < pageLayoutSize; j++) {
		if (freePages[j] > 0) {
			if (readPage(pageCount + j, data) == FILESYSTEM_ERROR) {
				free(data);
				goto endOfClaimFreePage;
			}
			for (int k = 0; k < intsPerPage; k++) {
				if (data[k] == UNUSED_PAGE) {
					free(data);
					result = j * intsPerPage + k;
					goto endOfClaimFreePage;
				}
			}
		}
	}
	for (int j = closeTo - 1; j >= 0; j--) {
		if (freePages[j] > 0) {
			if (readPage(pageCount + j, data) == FILESYSTEM_ERROR) {
				free(data);
				goto endOfClaimFreePage;
			}
			for (int k = 0; k < intsPerPage; k++) {
				if (data[k] == UNUSED_PAGE) {
					free(data);
					result = j * intsPerPage + k;
					goto endOfClaimFreePage;
				}
			}
		}
	}

	free(data);

	// If we come here, then no free page has been found. If pageCount has reached
	// its maximum value, return an error code.
	assert(pageCount < MAX_PAGE_COUNT);

	// Increase the size of the filesystem.
	oldPageCount = pageCount;
	if (pageCount <= SMALL_FILESYSTEM_THRESHOLD)
		newPageCount = (fs_pageno)(1.41 * 1.41 * oldPageCount);
	else
		newPageCount = (fs_pageno)(1.41 * oldPageCount);
	// obey some constraints on the number of pages in the system
	while (newPageCount % (pageSize / INT_SIZE) != 0)
		newPageCount++;
	if (newPageCount > MAX_PAGE_COUNT)
		newPageCount = MAX_PAGE_COUNT;
	if (changeSize(newPageCount) < 0)
		goto endOfClaimFreePage;
	
	result = claimFreePage(owner, oldPageCount);

endOfClaimFreePage:
	if (mustReleaseLock)
		releaseLock();

	return result;
} // end of claimFreePage(...)


fs_fileno FileSystem::claimFreeFileNumber() {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	// search for free file number
	for (int j = 0; j < fileMappingSize; j++) {
		if (freeFileNumbers[j] > 0) {
			int32_t *data = (int32_t*)malloc((intsPerPage + 1) * sizeof(int32_t));
			if (readPage(pageCount + pageLayoutSize + j, data) == FILESYSTEM_ERROR)
				goto endOfClaimFreeFileNumber;
			for (int k = 0; k < doubleIntsPerPage; k++)
				if (data[k * 2] == UNUSED_PAGE) {
					free(data);
					result = j * doubleIntsPerPage + k;
					goto endOfClaimFreeFileNumber;
				}
			free(data);
		}
	} // end for (int j = 0; j < fileMappingSize; j++)

	// No free slot could be found => Increase file size.
	if (increaseFileMappingSize() == FILESYSTEM_ERROR)
		goto endOfClaimFreeFileNumber;

	result = claimFreeFileNumber();

endOfClaimFreeFileNumber:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of claimFreeFileNumber()


int FileSystem::increaseFileMappingSize() {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	disableCaching();

	int32_t fileMappingSizeOnDisk;

	off_t fileSize = pageSize;
	fileSize *= (pageCount + pageLayoutSize + fileMappingSize + 1);
	if (ftruncate(dataFile, fileSize) < 0)
		goto endOfIncreaseFileMappingSize;
	if (getSize() != fileSize)
		goto endOfIncreaseFileMappingSize;

	fileMappingSize++;

	// write new fileMappingSize value to preamble
	fileMappingSizeOnDisk = fileMappingSize;
	lseek(dataFile, 4 * INT_SIZE, SEEK_SET);
	forced_write(dataFile, &fileMappingSizeOnDisk, INT_SIZE);

	// update the freeFileNumbers array
	free(freeFileNumbers);
	freeFileNumbers = NULL;
	for (int k = 0; k < doubleIntsPerPage; k++)
		setFirstPage((fileMappingSize - 1) * doubleIntsPerPage + k, UNUSED_PAGE);	
	initializeFreeSpaceArrays();

	enableCaching();

	result = FILESYSTEM_SUCCESS;

endOfIncreaseFileMappingSize:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of increaseFileMappingSize()


int FileSystem::decreaseFileMappingSize() {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	off_t fileSize;
	int32_t fileMappingSizeOnDisk;

	// do it only if the last page of the table is empty
	if (freeFileNumbers[fileMappingSize - 1] != doubleIntsPerPage)
		goto endOfDecreaseFileMappingSize;

	disableCaching();

	fileMappingSize--;

	// write new fileMappingSize value to preamble
	fileMappingSizeOnDisk = fileMappingSize;
	lseek(dataFile, 4 * INT_SIZE, SEEK_SET);
	forced_write(dataFile, &fileMappingSizeOnDisk, INT_SIZE);

	fileSize = pageSize;
	fileSize *= (pageCount + pageLayoutSize + fileMappingSize);
	forced_ftruncate(dataFile, fileSize);

	// update the freeFileNumbers array
	free(freeFileNumbers);
	freeFileNumbers = NULL;
	initializeFreeSpaceArrays();

	enableCaching();

	result = FILESYSTEM_SUCCESS;

endOfDecreaseFileMappingSize:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of decreaseFileMappingSize()


int FileSystem::createFile(fs_fileno fileHandle) {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	// Claim free page (first of file), then claim file number.
	fs_pageno firstPage = claimFreePage(-1, -1);
	assert(firstPage >= 0);

	while (fileHandle >= doubleIntsPerPage * fileMappingSize)
		increaseFileMappingSize();

	if (fileHandle >= 0) {
		// create a file with a specific file number
		if (getFirstPage(fileHandle) >= 0) {
			setPageStatus(firstPage, UNUSED_PAGE);
			goto endOfCreateFile;
		}
	}
	else {
		// create a file with an arbitrary file number
		fileHandle = claimFreeFileNumber();
		assert(fileHandle >= 0);
		setPageStatus(firstPage, UNUSED_PAGE);
	}

	// Set length to zero. Write first page to the file mappings table.
	setPageStatus(firstPage, 0);
	setFirstPage(fileHandle, firstPage);
	setPageCount(fileHandle, 1);

	result = fileHandle;

endOfCreateFile:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of createFile(fs_fileno)


fs_pageno FileSystem::getPageStatus(fs_pageno page) {
	bool mustReleaseLock = getLock();
	fs_pageno result = FILESYSTEM_ERROR;

	int pageInTable, pageToRead, offsetInPage;

	if ((page < 0) || (page >= pageCount))
		goto endOfGetPageStatus;

	pageInTable = page / intsPerPage;
	pageToRead = pageCount + pageInTable;
	offsetInPage = (page % intsPerPage) * INT_SIZE;

	if (readPage(pageToRead, offsetInPage, INT_SIZE, &result) == FILESYSTEM_ERROR)
		goto endOfGetPageStatus;

endOfGetPageStatus:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getPageStatus(fs_pageno)


int FileSystem::setPageStatus(fs_pageno page, fs_pageno newStatus) {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	int pageInTable, pageToWrite, offsetInPage;

	if ((page < 0) || (page >= pageCount))
		goto endOfSetPageStatus;

	pageInTable = page / intsPerPage;
	pageToWrite = pageCount + pageInTable;
	offsetInPage = (page % intsPerPage) * INT_SIZE;

	// read old value, write new value
	fs_pageno oldStatus;
	if (readPage(pageToWrite, offsetInPage, INT_SIZE, &oldStatus) == FILESYSTEM_ERROR)
		goto endOfSetPageStatus;
	if (writePage(pageToWrite, offsetInPage, INT_SIZE, &newStatus) == FILESYSTEM_ERROR)
		goto endOfSetPageStatus;

	// update the freePages array
	if ((oldStatus != newStatus) && (freePages != NULL)) {
		if (newStatus == UNUSED_PAGE)
			freePages[pageInTable]++;
		if (oldStatus == UNUSED_PAGE)
			freePages[pageInTable]--;
	}

	result = FILESYSTEM_SUCCESS;

endOfSetPageStatus:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of setPageStatus(fs_pageno, fs_pageno)


int32_t * FileSystem::getFilePageMapping(int *arraySize) {
	bool mustReleaseLock = getLock();

	int32_t *result = (int32_t*)malloc(fileMappingSize * doubleIntsPerPage * INT_SIZE);
	int32_t *buffer = (int32_t*)malloc(pageSize);
	int cnt = 0;
	for (int i = 0; i < fileMappingSize; i++) {
		readPage(pageCount + pageLayoutSize + i, buffer);
		for (int k = 0; k < doubleIntsPerPage; k++)
			result[cnt++] = buffer[k + k];
	}
	free(buffer);
	*arraySize = fileMappingSize * doubleIntsPerPage;

	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getFilePageMapping(int*)


fs_pageno FileSystem::getFirstPage(fs_fileno fileHandle) {
	bool mustReleaseLock = getLock();
	fs_pageno result = FILESYSTEM_ERROR;

	int pageInTable, pageToRead, offsetInPage;

	if ((fileHandle < 0) || (fileHandle >= fileMappingSize * doubleIntsPerPage))
		goto endOfGetFirstPage;

	pageInTable = fileHandle / doubleIntsPerPage;
	pageToRead = pageCount + pageLayoutSize + pageInTable;
	offsetInPage = (fileHandle % doubleIntsPerPage) * 2 * INT_SIZE;

	if (readPage(pageToRead, offsetInPage, INT_SIZE, &result) == FILESYSTEM_ERROR)
		goto endOfGetFirstPage;

endOfGetFirstPage:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getFirstPage(fs_fileno)


int FileSystem::setFirstPage(fs_fileno fileHandle, fs_pageno firstPage) {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	int pageInTable, pageToWrite, offsetInPage;

	if ((fileHandle < 0) || (fileHandle >= fileMappingSize * doubleIntsPerPage))
		goto endOfSetFirstPage;

	pageInTable = fileHandle / doubleIntsPerPage;
	pageToWrite = pageCount + pageLayoutSize + pageInTable;
	offsetInPage = (fileHandle % doubleIntsPerPage) * 2 * INT_SIZE;

	// read old value, write new value
	fs_pageno oldValue;
	if (readPage(pageToWrite, offsetInPage, INT_SIZE, &oldValue) == FILESYSTEM_ERROR)
		goto endOfSetFirstPage;
	if (writePage(pageToWrite, offsetInPage, INT_SIZE, &firstPage) == FILESYSTEM_ERROR)
		goto endOfSetFirstPage;

	// update the freeFileNumbers array
	if ((oldValue != firstPage) && (freeFileNumbers != NULL)) {
		if (firstPage == UNUSED_PAGE)
			freeFileNumbers[pageInTable]++;
		if (oldValue == UNUSED_PAGE)
			freeFileNumbers[pageInTable]--;
	}

	result = FILESYSTEM_SUCCESS;

endOfSetFirstPage:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of setFirstPage(fs_fileno, fs_pageno)


fs_pageno FileSystem::getPageCount(fs_fileno fileHandle) {
	bool mustReleaseLock = getLock();
	fs_pageno result = FILESYSTEM_ERROR;

	int pageInTable, pageToRead, offsetInPage;

	if ((fileHandle < 0) || (fileHandle >= fileMappingSize * doubleIntsPerPage))
		goto endOfGetPageCount;

	pageInTable = fileHandle / doubleIntsPerPage;
	pageToRead = pageCount + pageLayoutSize + pageInTable;
	offsetInPage = (fileHandle % doubleIntsPerPage) * 2 * INT_SIZE + INT_SIZE;

	if (readPage(pageToRead, offsetInPage, INT_SIZE, &result) == FILESYSTEM_ERROR)
		goto endOfGetPageCount;

endOfGetPageCount:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getPageCount(fs_fileno)


int FileSystem::setPageCount(fs_fileno fileHandle, fs_pageno newPageCount) {
	bool mustReleaseLock = getLock();
	int result = FILESYSTEM_ERROR;

	int pageInTable, pageToWrite, offsetInPage;

	if ((fileHandle < 0) || (fileHandle >= fileMappingSize * doubleIntsPerPage))
		goto endOfSetPageCount;

	pageInTable = fileHandle / doubleIntsPerPage;
	pageToWrite = pageCount + pageLayoutSize + pageInTable;
	offsetInPage = (fileHandle % doubleIntsPerPage) * 2 * INT_SIZE + INT_SIZE;

	result = writePage(pageToWrite, offsetInPage, INT_SIZE, &newPageCount);

endOfSetPageCount:
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of setPageCount(fs_fileno, fs_pageno)


int FileSystem::getPageSize() {
	return pageSize;
} // end of getPageSize()


off_t FileSystem::getSize() {
	if (dataFile < 0)
		return 0;
	struct stat buf;
	int status = fstat(dataFile, &buf);
	if (status != 0)
		return 0;
	return buf.st_size;
} // end of getSize()


int FileSystem::getFileCount() {
	bool mustReleaseLock = getLock();
	int result = 0;
	for (int j = 0; j < fileMappingSize; j++)
		result += (doubleIntsPerPage - freeFileNumbers[j]);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getFileCount()


int FileSystem::getPageCount() {
	return pageCount;
}


int FileSystem::getUsedPageCount() {
	bool mustReleaseLock = getLock();
	int result = 0;
	for (int j = 0; j < pageLayoutSize; j++)
		result += (intsPerPage - freePages[j]);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getUsedPageCount()


int FileSystem::readPage_UNCACHED(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer) {
	assert(dataFile >= 0);
	if (pageNumber >= pageCount + pageLayoutSize + fileMappingSize)
		return FILESYSTEM_ERROR;

	uncachedReadCnt++;

	// set read pointer to desired file offset
	off_t startPos = pageNumber;
	startPos *= pageSize;
	startPos += offset;
	lseek(dataFile, startPos, SEEK_SET);

	if (forced_read(dataFile, buffer, count) != count)
		return FILESYSTEM_ERROR;
	else
		return FILESYSTEM_SUCCESS;
} // end of readPage_UNCACHED(fs_pageno, int32_t, int32_t, void*)


int FileSystem::readPage(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer) {
	bool mustReleaseLock = getLock();
	cachedReadCnt++;
	int result;
	if (cache == NULL)
		result = readPage_UNCACHED(pageNumber, offset, count, buffer);
	else
		result = cache->readFromPage(pageNumber, offset, count, buffer);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of readPage(fs_pageno, int32_t, int32_t, void*)


int FileSystem::readPage(fs_pageno pageNumber, void *buffer) {
	return readPage(pageNumber, 0, pageSize, buffer);
}


int FileSystem::writePage_UNCACHED(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer) {
	assert(dataFile >= 0);
	if (pageNumber >= pageCount + pageLayoutSize + fileMappingSize)
		return FILESYSTEM_ERROR;

	uncachedWriteCnt++;

	// set write pointer to desired file offset
	off_t startPos = pageNumber;
	startPos *= pageSize;
	startPos += offset;
	lseek(dataFile, startPos, SEEK_SET);

	// write data to file
	if (forced_write(dataFile, buffer, count) != count)
		return FILESYSTEM_ERROR;
	else
		return FILESYSTEM_SUCCESS;
} // end of writePage_UNCACHED(fs_pageno, int32_t, int32_t, void*)


int FileSystem::writePage(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer) {
	bool mustReleaseLock = getLock();
	cachedWriteCnt++;
	int result;
	if (cache == NULL)
		result = writePage_UNCACHED(pageNumber, offset, count, buffer);
	else
		result = cache->writeToPage(pageNumber, offset, count, buffer);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of writePage(fs_pageno, int32_t, int32_t, void*)


int FileSystem::writePage(fs_pageno pageNumber, void *buffer) {
	return writePage(pageNumber, 0, pageSize, buffer);
}


void FileSystem::getCacheEfficiency(int64_t *reads, int64_t *uncachedReads,
			int64_t *writes, int64_t *uncachedWrites) {
	*reads = cachedReadCnt;
	*uncachedReads = uncachedReadCnt;
	*writes = cachedWriteCnt;
	*uncachedWrites = uncachedWriteCnt;
} // end of getCacheEfficiency(...)


char * FileSystem::getFileName() {
	return dataFileName;
}


//#define FILESYSTEM_DEBUG
#ifdef FILESYSTEM_DEBUG

#define TEST_FILES 13

int main(int argc, char **argv) {
	if (argc > 5) {
		fprintf(stderr, "Syntax:   filesystem DATA_FILE PAGE_SIZE PAGE_COUNT TEST_DATA\n\n");
		fprintf(stderr, "Creates a filesystem inside DATAFILE with given parameters and tests it using\n");
		fprintf(stderr, "Data taken from the file TEST_DATA.\n\n");
		return 1;
	}

	argv[1] = "../temp/data_file";
	argv[2] = "4096";
	argv[3] = "1024";
	argv[4] = "../temp/test_data";

	char *fileName = argv[1];
	int pageSize = atoi(argv[2]);
	int pageCount = atoi(argv[3]);
	char *testData = argv[4];
	FileSystem *fs = new FileSystem(fileName, pageSize, pageCount);
	if (!fs->isActive()) {
		fprintf(stderr, "Could not create filesystem.\n");
		return 1;
	}

	int inputFile = open(testData, O_RDONLY);

	File **f = new File*[256];
	for (int i = 0; i < TEST_FILES; i++) {
		f[i] = new File(fs);
		printf("new handle: %i. fileCount: %i\n", f[i]->getHandle(), fs->getFileCount());
	}
	f[4]->deleteFile();
	delete f[4];
	f[4] = new File(fs);
	printf("new handle: %i. fileCount: %i\n", f[4]->getHandle(), fs->getFileCount());

	int cnt = 0;
	char buffer[373];
	int inputFileSize = 0;
	while (true) {
		int size = forced_read(inputFile, buffer, sizeof(buffer));
		if (size == 0)
			break;
		for (int i = 0; i < TEST_FILES; i++)
			f[i]->write(size, buffer);
		inputFileSize += size;
	}

	char *inputData = (char*)malloc(inputFileSize);
	lseek(inputFile, 0, SEEK_SET);
	forced_read(inputFile, inputData, inputFileSize);

	printf("Total pages: %i. Used: %i.\n", fs->getPageCount(), fs->getUsedPageCount());

	int64_t a, b, c, d;
	fs->getCacheEfficiency(&a, &b, &c, &d);
	printf("Cache efficiency: %i/%i reads, %i/%i writes.\n",
			(int)b, (int)a, (int)d, (int)c);

	for (int i = 0; i < TEST_FILES; i++) {
		lseek(inputFile, 0, SEEK_SET);
		f[i]->seek(0);
		printf("f[%i]->getSize() = %i\n", i, f[i]->getSize());
		for (int j = 0; j < f[i]->getSize(); j++) {
			char c;
			f[i]->read(1, &c);
			if (c != inputData[j])
				fprintf(stderr, "ERROR!\n");
		}
		printf("  File %i ok.\n", i);
	}

	printf("Before closing: %i %i %i.\n", fs->getPageSize(), fs->getPageCount(), fs->getFileCount());
	// close files and close filesystem
	for (int i = 0; i < TEST_FILES; i++)
		delete f[i];
	delete fs;
/*
	fs = new FileSystem(fileName);
	if (fs->defrag() == FILESYSTEM_ERROR) {
		fprintf(stderr, "defrag() returns error code.\n");
		return 1;
	}
	delete fs;
*/
	for (int loop = 0; loop < 256; loop++) {
		fs = new FileSystem(fileName);
		printf("After reopening: %i %i %i.\n", fs->getPageSize(), fs->getPageCount(), fs->getFileCount());
		for (int i = 0; i < TEST_FILES; i++) {
			lseek(inputFile, 0, SEEK_SET);
			f[i] = new File(fs, i, false);
			f[i]->seek(0);
			for (int j = 0; j < f[i]->getSize(); j++) {
				char c;
				f[i]->read(1, &c);
				if (c != inputData[j])
					fprintf(stderr, "ERROR!\n");
			}
			delete f[i];
		}
		delete fs;
	} // end for (int loop = 0; loop < 1; loop++)
	
	return 0;
} // end of main(int, char**)


#endif

