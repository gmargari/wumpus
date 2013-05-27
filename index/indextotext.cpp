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
 * Implementation of the IndexToText class.
 *
 * author: Stefan Buettcher
 * created: 2005-03-08
 * changed: 2009-02-01
 **/


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "indextotext.h"
#include "../misc/all.h"


static const char *INDEXTOTEXT_FILE = "index.map";

static const char *LOG_ID = "IndexToText";


IndexToText::IndexToText(const char *fileName) {
	getConfigurationBool("READ_ONLY", &readOnly, false);
	if (readOnly) {
		log(LOG_ERROR, LOG_ID, "Unable to create index-to-text map while in read-only mode.");
		exit(1);
	}

	// create a new IndexToText instance
	this->fileName = duplicateString(fileName);
	int flags = O_RDWR | O_CREAT | O_TRUNC;
	fileHandle = open(fileName, flags, DEFAULT_FILE_PERMISSIONS);
	if (fileHandle < 0) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to create data file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
	numberOfMappingsOnDisk = 0;
	numberOfMappingsInMemory = 0;
	memorySlotsAllocated = 1024;
	inMemoryMappings = typed_malloc(InMemoryMapping, memorySlotsAllocated);
	if (fileHandle < 0)
		return;
	lseek(fileHandle, 0, SEEK_SET);
	forced_write(fileHandle, &numberOfMappingsOnDisk, sizeof(numberOfMappingsOnDisk));
	forced_write(fileHandle, &numberOfMappingsInMemory, sizeof(numberOfMappingsInMemory));
} // end of IndexToText(char*)


IndexToText::IndexToText(const char *workDirectory, bool create) {
	getConfigurationBool("READ_ONLY", &readOnly, false);

	fileName = evaluateRelativePathName(workDirectory, INDEXTOTEXT_FILE);
	if (create) {
		if (readOnly) {
			log(LOG_ERROR, LOG_ID, "Unable to create index-to-text map while in read-only mode.");
			exit(1);
		}
	
		// create new IndexToText instance
		int flags = O_RDWR | O_CREAT | O_TRUNC;
		fileHandle = open(fileName, flags, DEFAULT_FILE_PERMISSIONS);
		if (fileHandle < 0) {
			snprintf(errorMessage, sizeof(errorMessage), "Unable to create data file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
		}
		numberOfMappingsOnDisk = 0;
		numberOfMappingsInMemory = 0;
		memorySlotsAllocated = 1024;
		inMemoryMappings = typed_malloc(InMemoryMapping, memorySlotsAllocated);
		if (fileHandle < 0)
			return;
		lseek(fileHandle, (off_t)0, SEEK_SET);
		forced_write(fileHandle, &numberOfMappingsOnDisk, sizeof(numberOfMappingsOnDisk));
		forced_write(fileHandle, &numberOfMappingsInMemory, sizeof(numberOfMappingsInMemory));
	} // end if create
	else {
		// resume from old data stored in the data file
		fileHandle = open(fileName, readOnly ? O_RDONLY : O_RDWR);
		if (fileHandle < 0) {
			snprintf(errorMessage, sizeof(errorMessage), "Unable to open data file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			numberOfMappingsOnDisk = 0;
			numberOfMappingsInMemory = 0;
			memorySlotsAllocated = 1024;
			inMemoryMappings = typed_malloc(InMemoryMapping, memorySlotsAllocated);
			return;
		}
		lseek(fileHandle, (off_t)0, SEEK_SET);
		forced_read(fileHandle, &numberOfMappingsOnDisk, sizeof(numberOfMappingsOnDisk));
		lseek(fileHandle, numberOfMappingsOnDisk * sizeof(OnDiskMapping), SEEK_CUR);
		forced_read(fileHandle, &numberOfMappingsInMemory, sizeof(numberOfMappingsInMemory));
		memorySlotsAllocated = (numberOfMappingsInMemory / 1024) * 1024 + 2048;
		inMemoryMappings = typed_malloc(InMemoryMapping, memorySlotsAllocated);
		forced_read(fileHandle, inMemoryMappings, numberOfMappingsInMemory * sizeof(InMemoryMapping));
	} // end else [!create]
} // end of IndexToText(char*, bool)


IndexToText::~IndexToText() {
	if (fileHandle >= 0) {
		if (!readOnly)
			saveToDisk();
		close(fileHandle);
		fileHandle = -1;
	}
	FREE_AND_SET_TO_NULL(fileName);
	FREE_AND_SET_TO_NULL(inMemoryMappings);
} // end of ~IndexToText()


void IndexToText::saveToDisk() {
	LocalLock lock(this);
	lseek(fileHandle, 0, SEEK_SET);
	forced_write(fileHandle, &numberOfMappingsOnDisk, sizeof(numberOfMappingsOnDisk));
	lseek(fileHandle, numberOfMappingsOnDisk * sizeof(OnDiskMapping), SEEK_CUR);
	forced_write(fileHandle, &numberOfMappingsInMemory, sizeof(numberOfMappingsInMemory));
	forced_write(fileHandle, inMemoryMappings, numberOfMappingsInMemory * sizeof(InMemoryMapping));
	forced_ftruncate(fileHandle, lseek(fileHandle, 0, SEEK_CUR));
} // end of saveToDisk()


void IndexToText::addMapping(offset fileStart, TokenPositionPair tpp) {	
	LocalLock lock(this);

	OnDiskMapping odm = { fileStart + tpp.sequenceNumber, tpp.filePosition };
	off_t filePos =
		sizeof(numberOfMappingsOnDisk) + numberOfMappingsOnDisk * sizeof(OnDiskMapping);
	lseek(fileHandle, filePos, SEEK_SET);
	int result = forced_write(fileHandle, &odm, sizeof(odm));
	if (result != sizeof(odm)) {
		sprintf(errorMessage,
				"result = %d, sizeof(odm) = %d\n", result, static_cast<int>(sizeof(odm)));
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
	assert(result == sizeof(odm));
	if (numberOfMappingsOnDisk % INDEX_GRANULARITY == 0) {
		// add a new in-memory mapping
		if (numberOfMappingsInMemory >= memorySlotsAllocated) {
			// allocate new memory if necessary
			memorySlotsAllocated += 2048;
			inMemoryMappings =
				typed_realloc(InMemoryMapping, inMemoryMappings, memorySlotsAllocated);
		}
		int current = numberOfMappingsInMemory;
		inMemoryMappings[current].positionInMapping = numberOfMappingsOnDisk;
		inMemoryMappings[current].indexPosition = odm.indexPosition;
		inMemoryMappings[current].chunkSize = 0;
		numberOfMappingsInMemory++;
	}
	inMemoryMappings[numberOfMappingsInMemory - 1].chunkSize++;
	numberOfMappingsOnDisk++;
} // end of addMapping(offset, TokenPositionPair)


void IndexToText::addMappings(offset fileStart, TokenPositionPair *tpp, int count) {
	LocalLock lock(this);

	off_t filePos =
		sizeof(numberOfMappingsOnDisk) + numberOfMappingsOnDisk * sizeof(OnDiskMapping);
	lseek(fileHandle, filePos, SEEK_SET);
	for (int i = 0; i < count; i++) {
		OnDiskMapping odm = { fileStart + tpp[i].sequenceNumber, tpp[i].filePosition };
		int result = forced_write(fileHandle, &odm, sizeof(odm));
		if (result != sizeof(odm)) {
			sprintf(errorMessage, "result = %d, sizeof(odm) = %d\n",
					result, static_cast<int>(sizeof(odm)));
			log(LOG_ERROR, LOG_ID, errorMessage);
		}
		assert(result == sizeof(odm));
		if (numberOfMappingsOnDisk % INDEX_GRANULARITY == 0) {
			// add a new in-memory mapping
			if (numberOfMappingsInMemory >= memorySlotsAllocated) {
				// allocate new memory if necessary
				memorySlotsAllocated += 2048;
				inMemoryMappings =
					typed_realloc(InMemoryMapping, inMemoryMappings, memorySlotsAllocated);
			}
			int current = numberOfMappingsInMemory;
			inMemoryMappings[current].positionInMapping = numberOfMappingsOnDisk;
			inMemoryMappings[current].indexPosition = odm.indexPosition;
			inMemoryMappings[current].chunkSize = 0;
			numberOfMappingsInMemory++;
		}
		inMemoryMappings[numberOfMappingsInMemory - 1].chunkSize++;
		numberOfMappingsOnDisk++;
	}
} // end of addMappings(offset, TokenPositionPair*, int)


bool IndexToText::getLastSmallerEq(offset where, offset *indexPosition, off_t *filePosition) {
	LocalLock lock(this);

	if ((numberOfMappingsOnDisk == 0) || (fileHandle < 0))
		return false;

	int cnt = numberOfMappingsInMemory;
	if (inMemoryMappings[0].indexPosition > where)
		return false;
	
	// find the correct segment of the on-disk data by doing a binary search on
	// the in-memory data
	int lower = 0;
	int upper = cnt - 1;
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (inMemoryMappings[middle].indexPosition > where)
			upper = middle - 1;
		else
			lower = middle;
	}

	// seek to the correct position on disk, read data and do a linear search
	off_t pos = inMemoryMappings[lower].positionInMapping;
	pos = sizeof(numberOfMappingsOnDisk) + pos * sizeof(OnDiskMapping);
	lseek(fileHandle, pos, SEEK_SET);
	OnDiskMapping *odm = typed_malloc(OnDiskMapping, inMemoryMappings[lower].chunkSize);
	forced_read(fileHandle, odm, inMemoryMappings[lower].chunkSize * sizeof(OnDiskMapping));
	for (int i = 0; i < inMemoryMappings[lower].chunkSize; i++) {
		if (odm[i].indexPosition <= where) {
			*indexPosition = odm[i].indexPosition;
			*filePosition = odm[i].filePosition;
		}
	}
	free(odm);

	return true;
} // end of getLastSmallerEq(offset, offset*, off_t*)


void IndexToText::filterAgainstFileList(ExtentList *files) {
	LocalLock lock(this);

	char *tempFileName = concatenateStrings(fileName, ".temp");

	// create new IndexToText instance and push all data from this object that lies
	// within the given files into the new object	
	IndexToText *newInstance = new IndexToText(tempFileName);
	offset currentFileStart = -1, currentFileEnd = -1;
	lseek(fileHandle, sizeof(numberOfMappingsOnDisk), SEEK_SET);
	static const int BLOCKSIZE = 256;
	OnDiskMapping *block = typed_malloc(OnDiskMapping, BLOCKSIZE);
	for (int b = 0; b < numberOfMappingsOnDisk; b += BLOCKSIZE) {
		int blockSize = numberOfMappingsOnDisk - b;
		if (blockSize > BLOCKSIZE)
			blockSize = BLOCKSIZE;
		forced_read(fileHandle, block, sizeof(OnDiskMapping) * blockSize);
		for (int i = 0; i < blockSize; i++) {
			offset where = block[i].indexPosition;
			if (where > currentFileEnd) {
				if (!files->getFirstEndBiggerEq(where, &currentFileStart, &currentFileEnd)) {
					currentFileStart = MAX_OFFSET;
					break;
				}
			}
			if (where >= currentFileStart) {
				TokenPositionPair tpp = { 0, block[i].filePosition };
				newInstance->addMapping(block[i].indexPosition, tpp);
			}
		}
		if (currentFileStart == MAX_OFFSET)
			break;
	}
	free(block);
	delete newInstance;

	// close new file; delete old file; change name of data file; open new file
	close(fileHandle);
	free(inMemoryMappings);
	unlink(fileName);
	rename(tempFileName, fileName);
	free(tempFileName);
	fileHandle = open(fileName, O_RDWR);

	// reload data from disk
	lseek(fileHandle, (off_t)0, SEEK_SET);
	forced_read(fileHandle, &numberOfMappingsOnDisk, sizeof(numberOfMappingsOnDisk));
	lseek(fileHandle, numberOfMappingsOnDisk * sizeof(OnDiskMapping), SEEK_CUR);
	forced_read(fileHandle, &numberOfMappingsInMemory, sizeof(numberOfMappingsInMemory));
	memorySlotsAllocated = (numberOfMappingsInMemory / 1024) * 1024 + 2048;
	inMemoryMappings = typed_malloc(InMemoryMapping, memorySlotsAllocated);
	forced_read(fileHandle, inMemoryMappings, numberOfMappingsInMemory * sizeof(InMemoryMapping));
} // end of filterAgainstFileList(ExtentList*)



