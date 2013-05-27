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
 * Implementation of the class BucketFileSystem.
 * Documentation can be found in file "bucketfilesystem.h".
 *
 * author: Stefan Buettcher
 * created: 2004-10-15
 * changed: 2009-02-01
 **/


#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "bucketfilesystem.h"
#include "../index/index_types.h"
#include "../misc/alloc.h"
#include "../misc/assert.h"
#include "../misc/io.h"
#include "../misc/logging.h"


static const char * LOG_ID = "BucketFileSystem";


BucketFileSystem::BucketFileSystem(char *fileName) {

	dataFileName = (char*)malloc(strlen(fileName) + 2);
	strcpy(dataFileName, fileName);

	// open file, set internal variables
	dataFile = open(fileName, FileSystem::FILESYSTEM_ACCESS);
	if (dataFile < 0) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Filesystem \"%s\" could not be opened.", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		perror(NULL);
		return;
	}

	// read preamble from disk
	int32_t *pageBuffer = (int32_t*)malloc(4 * sizeof(int32_t));
	lseek(dataFile, 0, SEEK_SET);
	if (forced_read(dataFile, pageBuffer, 4 * sizeof(int32_t)) != 4 * sizeof(int32_t)) {
		close(dataFile);
		dataFile = -1;
		snprintf(errorMessage, sizeof(errorMessage),
				"Could not read preamble from filesystem \"%s\".", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		perror(NULL);
		return;
	}
	int32_t fingerprintOnDisk = pageBuffer[0];
	int32_t bucketSizeOnDisk = pageBuffer[1];
	int32_t bucketCountOnDisk = pageBuffer[2];
	free(pageBuffer);

	bucketCount = bucketCountOnDisk;
	bucketSize = bucketSizeOnDisk;

	// check if we have valid data
	if (fingerprintOnDisk != FINGERPRINT) {
		close(dataFile);
		dataFile = -1;
		return;
	}

	timeStamp = 0;
	for (int i = 0; i < CACHE_SIZE; i++) {
		cache[i].changed = false;
		cache[i].bucket = -1;
		cache[i].timeStamp = 0;
		cache[i].data = (char*)malloc(bucketSize);
	}

} // end of BucketFileSystem(char*)


BucketFileSystem::BucketFileSystem(char *fileName, int bucketSize, int bucketCount) {

	dataFileName = (char*)malloc(strlen(fileName) + 2);
	strcpy(dataFileName, fileName);

	// open file, set internal variables
	dataFile = open(fileName,
			O_CREAT | O_TRUNC | FileSystem::FILESYSTEM_ACCESS, DEFAULT_FILE_PERMISSIONS);
	if (dataFile < 0) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to create filesystem \"%s\".", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		perror(NULL);
		return;
	}
	this->bucketSize = bucketSize;
	this->bucketCount = bucketCount;

	off_t fileSize = (bucketCount + 1);
	fileSize *= bucketSize;
	if (ftruncate(dataFile, fileSize) < 0) {
		log(LOG_ERROR, LOG_ID, "Unable fo set filesystem size.");
		perror(NULL);
		close(dataFile);
		dataFile = -1;
		return;
	}
	if (getSize() != fileSize) {
		log(LOG_ERROR, LOG_ID, "Unable fo set filesystem size.");
		perror(NULL);
		close(dataFile);
		dataFile = -1;
		return;
	}
	
	// write preamble to disk
	int32_t fingerprintOnDisk = FINGERPRINT;
	int32_t bucketSizeOnDisk = bucketSize;
	int32_t bucketCountOnDisk = bucketCount;
	lseek(dataFile, 0, SEEK_SET);
	forced_write(dataFile, &fingerprintOnDisk, sizeof(int32_t));
	forced_write(dataFile, &bucketSizeOnDisk, sizeof(int32_t));
	forced_write(dataFile, &bucketCountOnDisk, sizeof(int32_t));

	timeStamp = 0;
	for (int i = 0; i < CACHE_SIZE; i++) {
		cache[i].changed = false;
		cache[i].bucket = -1;
		cache[i].timeStamp = 0;
		cache[i].data = (char*)malloc(bucketSize);
	}

} // end of BucketFileSystem(char*, int, int)


BucketFileSystem::~BucketFileSystem() {
	for (int i = 0; i < CACHE_SIZE; i++) {
		if (cache[i].changed)
			writeCacheSlot(i);
		if (cache[i].data != NULL)
			free(cache[i].data);
	}
	if (dataFile >= 0) {
		// close data file
		close(dataFile);
		dataFile = -1;
	}
	if (dataFileName != NULL) {
		free(dataFileName);
		dataFileName = NULL;
	}
} // end of ~BucketFileSystem()


bool BucketFileSystem::isActive() {
	return (dataFile >= 0);
} // end of isActive()


int BucketFileSystem::changeSize(int newBucketCount) {
	bool mustReleaseLock = getWriteLock();
	int result = FILESYSTEM_ERROR;
	assert(newBucketCount >= bucketCount);

	off_t fileSize;
	int32_t bucketCountOnDisk;

	if (newBucketCount == bucketCount) {
		result = FILESYSTEM_SUCCESS;
		goto endOfChangeSize;
	}

	// change the size of the data file
	fileSize = (newBucketCount + 1);
	fileSize *= bucketSize;
	if (ftruncate(dataFile, fileSize) < 0) {
		log(LOG_ERROR, LOG_ID, "Unable to change filesystem size.");
		perror(NULL);
		goto endOfChangeSize;
	}
	if (getSize() != fileSize) {
		log(LOG_ERROR, LOG_ID, "Unable to change filesystem size.");
		perror(NULL);
		goto endOfChangeSize;
	}
	bucketCount = newBucketCount;

	// write changed preamble to disk
	bucketCountOnDisk = bucketCount;
	lseek(dataFile, 2 * sizeof(int32_t), SEEK_SET);
	forced_write(dataFile, &bucketCountOnDisk, sizeof(int32_t));

	result = FILESYSTEM_SUCCESS;

endOfChangeSize:
	if (mustReleaseLock)
		releaseWriteLock();
	return result;
} // end of changeSize(int)


int BucketFileSystem::getBucketSize() {
	return bucketSize;
} // end of getBucketSize()


off_t BucketFileSystem::getSize() {
	if (dataFile < 0)
		return 0;
	struct stat buf;
	int status = fstat(dataFile, &buf);
	if (status != 0)
		return 0;
	return buf.st_size;
} // end of getSize()


int BucketFileSystem::getBucketCount() {
	return bucketCount;
}


char * BucketFileSystem::getFileName() {
	return dataFileName;
}


int BucketFileSystem::readBucket(int bucket, char *data) {
	off_t off;
	bool mustReleaseLock = getWriteLock();
	int result = FILESYSTEM_ERROR;
	
	timeStamp++;
	if (bucket >= bucketCount) {
		int result;
		if (bucket > GROWTH_RATE * bucketCount)
			result = changeSize(bucket + 2);
		else
			result = changeSize((int)(GROWTH_RATE * bucketCount));
		assert(result == FILESYSTEM_SUCCESS);
	}
	for (int i = 0; i < CACHE_SIZE; i++)
		if (cache[i].bucket == bucket) {
			cache[i].timeStamp = timeStamp;
			memcpy(data, cache[i].data, bucketSize);
			result = FILESYSTEM_SUCCESS;
			goto endOfReadBucket;
		}
	off = (bucket + 1);
	off *= bucketSize;
	lseek(dataFile, off, SEEK_SET);
	if (forced_read(dataFile, data, bucketSize) == bucketSize) {
		int oldest = 0;
		for (int i = 1; i < CACHE_SIZE; i++)
			if (cache[i].timeStamp < cache[oldest].timeStamp)
				oldest = i;
		if (cache[oldest].changed) {
			writeCacheSlot(oldest);
			cache[oldest].changed = false;
		}
		memcpy(cache[oldest].data, data, bucketSize);
		cache[oldest].bucket = bucket;
		cache[oldest].timeStamp = timeStamp;
		result = FILESYSTEM_SUCCESS;
		goto endOfReadBucket;
	}

endOfReadBucket:
	if (mustReleaseLock)
		releaseWriteLock();
	return result;
} // end of readBucket(int, char*)


int BucketFileSystem::writeBucket(int bucket, char *data) {
	int oldest = 0;
	bool mustReleaseLock = getWriteLock();
	int result = FILESYSTEM_ERROR;

	timeStamp++;
	if (bucket >= bucketCount) {
		int result;
		if (bucket > GROWTH_RATE * bucketCount - 2)
			result = changeSize(bucket + 2);
		else
			result = changeSize((int)(GROWTH_RATE * bucketCount));
		assert(result == FILESYSTEM_SUCCESS);
	}
	for (int i = 0; i < CACHE_SIZE; i++)
		if (cache[i].bucket == bucket) {
			cache[i].timeStamp = timeStamp;
			cache[i].changed = true;
			memcpy(cache[i].data, data, bucketSize);
			result = FILESYSTEM_SUCCESS;
			goto endOfWriteBucket;
		}
	for (int i = 1; i < CACHE_SIZE; i++)
		if (cache[i].timeStamp < cache[oldest].timeStamp)
			oldest = i;
	if (cache[oldest].changed)
		writeCacheSlot(oldest);
	memcpy(cache[oldest].data, data, bucketSize);
	cache[oldest].bucket = bucket;
	cache[oldest].timeStamp = timeStamp;
	cache[oldest].changed = true;
	result = FILESYSTEM_SUCCESS;

endOfWriteBucket:
	if (mustReleaseLock)
		releaseWriteLock();
	return result;
} // end of writeBucket(int, char*)


int BucketFileSystem::writeBucket(int bucket, char *data, int offset, int count) {
	bool mustReleaseLock = getWriteLock();
	int result = FILESYSTEM_ERROR;

	timeStamp++;
	if (bucket >= bucketCount) {
		int result;
		if (bucket > GROWTH_RATE * bucketCount - 2)
			result = changeSize(bucket + 2);
		else
			result = changeSize((int)(GROWTH_RATE * bucketCount));
		assert(result == FILESYSTEM_SUCCESS);
	}
	int i;
	char *tempData;
writeTryAgain:
	for (i = 0; i < CACHE_SIZE; i++)
		if (cache[i].bucket == bucket) {
			cache[i].timeStamp = timeStamp;
			cache[i].changed = true;
			memcpy(&cache[i].data[offset], data, count);
			result = FILESYSTEM_SUCCESS;
			goto endOfWriteBucket;
		}
	tempData = (char*)malloc(bucketSize);
	readBucket(bucket, tempData);
	free(tempData);
	goto writeTryAgain;

endOfWriteBucket:
	if (mustReleaseLock)
		releaseWriteLock();
	return result;
} // end of writeBucket(int, char*, int, int)


void BucketFileSystem::writeCacheSlot(int slot) {
	int bucket = cache[slot].bucket;
	off_t off = (bucket + 1);
	off *= bucketSize;
	lseek(dataFile, off, SEEK_SET);
	forced_write(dataFile, cache[slot].data, bucketSize);
} // end of writeCacheSlot(int)



