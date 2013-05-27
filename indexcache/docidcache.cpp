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
 * Implementation of the DocIdCache class.
 *
 * author: Stefan Buettcher
 * created: 2005-05-29
 * changed: 2009-02-02
 **/


#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "docidcache.h"
#include "../misc/all.h"

#define USE_ZLIB 1

#if USE_ZLIB
#include <zlib.h>
#endif


static const char * DATA_FILE = "index.docids";

static const char * LOG_ID = "DocIdCache";


DocIdCache::DocIdCache() {
	fileHandle = -1;
}


DocIdCache::DocIdCache(const char *path, bool isDirectory) {
	getConfigurationBool("READ_ONLY", &readOnly, false);

	if (isDirectory)
		fileName = evaluateRelativePathName(path, DATA_FILE);
	else
		fileName = duplicateString(path);

	fileHandle = open(fileName, readOnly ? O_RDONLY : O_RDWR);
	if (fileHandle >= 0)
		loadFromDisk();
	else if (readOnly) {
		log(LOG_ERROR, LOG_ID, "Unable to create new docid cache while in read-only mode.");
		exit(1);
	}
	else {
		documentCount = 0;
		bucketCount = 0;
		fileHandle = open(fileName, O_RDWR | O_CREAT, DEFAULT_FILE_PERMISSIONS);
		if (fileHandle < 0) {
			snprintf(errorMessage, sizeof(errorMessage), "Unable to create new file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			free(fileName);
			return;
		}
		bucketsAllocated = 8;
		bucketSize = typed_malloc(int, bucketsAllocated);
		positions = typed_malloc(offset, bucketsAllocated);
		docIdBuckets = typed_malloc(char*, bucketsAllocated);
		currentBucketSize = 0;
		currentBucketAllocated = INITIAL_BUCKET_SIZE;
		currentBucketFirstPos = 0;
		currentBucketLastPos = 0;
		currentBucket = typed_malloc(char, currentBucketAllocated);
		saveToDisk();
	}
	modified = false;
	mruBucket = -1;
	mruBucketData = NULL;
} // end of DocIdCache(char*)


DocIdCache::~DocIdCache() {
	if (fileHandle < 0)
		return;
	if (modified)
		saveToDisk();
	releaseAllResources();
} // end of ~DocIdCache()


void DocIdCache::releaseAllResources() {
	if (fileHandle >= 0) {
		close(fileHandle);
		fileHandle = -1;
	}
	for (int i = 0; i < bucketCount; i++)
		free(docIdBuckets[i]);
	FREE_AND_SET_TO_NULL(docIdBuckets);
	FREE_AND_SET_TO_NULL(currentBucket);
	FREE_AND_SET_TO_NULL(bucketSize);
	FREE_AND_SET_TO_NULL(positions);
	FREE_AND_SET_TO_NULL(mruBucketData);
	FREE_AND_SET_TO_NULL(fileName);
} // end of releaseAllResources()


void DocIdCache::saveToDisk() {
	lseek(fileHandle, (off_t)0, SEEK_SET);
	forced_ftruncate(fileHandle, (off_t)0);
	forced_write(fileHandle, &documentCount, sizeof(documentCount));
	forced_write(fileHandle, &bucketCount, sizeof(bucketCount));
	forced_write(fileHandle, bucketSize, bucketCount * sizeof(int));
	forced_write(fileHandle, positions, bucketCount * sizeof(offset));
	for (int i = 0; i < bucketCount; i++)
		forced_write(fileHandle, docIdBuckets[i], bucketSize[i]);
	forced_write(fileHandle, &currentBucketSize, sizeof(currentBucketSize));
	forced_write(fileHandle, &currentBucketAllocated, sizeof(currentBucketAllocated));
	forced_write(fileHandle, &currentBucketFirstPos, sizeof(currentBucketFirstPos));
	forced_write(fileHandle, &currentBucketLastPos, sizeof(currentBucketLastPos));
	forced_write(fileHandle, currentBucket, currentBucketAllocated);
	modified = false;
} // end of saveToDisk()


void DocIdCache::loadFromDisk() {
	lseek(fileHandle, (off_t)0, SEEK_SET);
	forced_read(fileHandle, &documentCount, sizeof(documentCount));
	forced_read(fileHandle, &bucketCount, sizeof(bucketCount));
	bucketsAllocated = bucketCount + 1;
	bucketSize = typed_malloc(int, bucketsAllocated);
	positions = typed_malloc(offset, bucketsAllocated);
	docIdBuckets = typed_malloc(char*, bucketsAllocated);
	forced_read(fileHandle, bucketSize, bucketCount * sizeof(int));
	forced_read(fileHandle, positions, bucketCount * sizeof(offset));
	for (int i = 0; i < bucketCount; i++) {
		docIdBuckets[i] = typed_malloc(char, bucketSize[i]);
		forced_read(fileHandle, docIdBuckets[i], bucketSize[i]);
	}
	forced_read(fileHandle, &currentBucketSize, sizeof(currentBucketSize));
	forced_read(fileHandle, &currentBucketAllocated, sizeof(currentBucketAllocated));
	forced_read(fileHandle, &currentBucketFirstPos, sizeof(currentBucketFirstPos));
	forced_read(fileHandle, &currentBucketLastPos, sizeof(currentBucketLastPos));
	currentBucket = typed_malloc(char, currentBucketAllocated);
	forced_read(fileHandle, currentBucket, currentBucketAllocated);
	modified = false;
} // end of loadFromDisk()


void DocIdCache::addDocumentID(offset documentStart, char *id) {
	// only proceed if docid caching has been enabled in the config file
	if ((!TREC_DOCNO_CACHING) || (readOnly))
		return;

	int len = strlen(id);
	if (len > MAX_DOCID_LEN) {
		snprintf(errorMessage, sizeof(errorMessage), "ID too long: %s", id);
		log(LOG_ERROR, LOG_ID, errorMessage);
		return;
	}
	if (currentBucketSize + len + 10 > currentBucketAllocated) {
		currentBucketAllocated *= 2;
		currentBucket = (char*)realloc(currentBucket, currentBucketAllocated);
	}

	// put "documentStart" into bucket as delta value
	offset delta = documentStart - currentBucketLastPos;
	while (delta >= 128) {
		currentBucket[currentBucketSize++] = (delta & 127) | 128;
		delta >>= 7;
	}
	currentBucket[currentBucketSize++] = delta;
	if (currentBucketFirstPos == 0)
		currentBucketFirstPos = documentStart;
	currentBucketLastPos = documentStart;

	// put the document ID itself into the bucket
	strcpy(&currentBucket[currentBucketSize], id);
	currentBucketSize += len + 1;

	if (++documentCount % IDS_PER_BUCKET == 0) {
		if (bucketCount >= bucketsAllocated) {
			bucketsAllocated *= 2;
			typed_realloc(int, bucketSize, bucketsAllocated);
			typed_realloc(offset, positions, bucketsAllocated);
			typed_realloc(char*, docIdBuckets, bucketsAllocated);
		}
		byte *compressed = typed_malloc(byte, currentBucketAllocated);
		int compressedSize = -1;
#if USE_ZLIB
		uLongf destLen = currentBucketAllocated + 256;
		if (compress((Bytef*)compressed, &destLen, (Bytef*)currentBucket,
					(uLong)currentBucketSize) == Z_OK)
			compressedSize = (int)destLen;
#else
		compressLZW((byte*)currentBucket, compressed, currentBucketSize,
				&compressedSize, currentBucketAllocated);
#endif
		if (compressedSize < 0) {
			log(LOG_ERROR, LOG_ID, "Failed to compress docid buffer.");
			assert(false);
		}
		bucketSize[bucketCount] = compressedSize;
		docIdBuckets[bucketCount] = (char*)realloc(compressed, compressedSize);
		positions[bucketCount] = currentBucketFirstPos;
		bucketCount++;
		free(currentBucket);
		currentBucketAllocated = INITIAL_BUCKET_SIZE;
		currentBucket = typed_malloc(char, currentBucketAllocated);
		currentBucketSize = 0;
		currentBucketFirstPos = currentBucketLastPos = 0;
	}
	modified = true;
} // end of addDocumentID(char*)


char * DocIdCache::getDocumentID(offset documentStart) {
	LocalLock lock(this);

	char *result = NULL;
	int whichBucket = -1;
	if (documentCount == 0)
		return NULL;

	if ((documentStart >= currentBucketFirstPos) && (currentBucketFirstPos > 0)) {
		// grab document ID directly from current, uncompressed bucket
		result = extractID((byte*)currentBucket, documentStart, currentBucketSize);
		mruBucket = -1;
		positionInMruBucket = -1;
		offsetOfMruDocument = -1;
	}
	else if ((bucketCount > 0) && (positions[bucketCount - 1] <= documentStart)) {
		// search for document ID in last compressed bucket
		whichBucket = bucketCount - 1;
	}
	else if ((bucketCount > 0) && (positions[0] <= documentStart)) {
		// search for appropriate bucket
		if ((mruBucket >= 0) && (mruBucket < bucketCount - 1))
			if ((positions[mruBucket] <= documentStart) && (positions[mruBucket + 1] > documentStart))
				whichBucket = mruBucket;
		if (whichBucket < 0) {
			int lower = 0;
			int upper = bucketCount - 1;
			while (upper > lower) {
				int middle = (lower + upper + 1) >> 1;
				if (positions[middle] > documentStart)
					upper = middle - 1;
				else
					lower = middle;
			}
			whichBucket = lower;
		}
	} // end else if ((bucketCount > 0) && (positions[0] <= documentStart))

	if ((result == NULL) && (whichBucket >= 0) && (bucketCount >= 0)) {
		if (whichBucket != mruBucket)
			loadBucket(whichBucket);
		if (positionInMruBucket >= 0)
			result = extractID(mruBucketData, documentStart, mruBucketSize,
					positionInMruBucket, offsetOfMruDocument);
		else
			result = extractID(mruBucketData, documentStart, mruBucketSize);
	} // end if ((result == NULL) && (whichBucket >= 0))

	return result;
} // end of getDocumentID(offset)


char * DocIdCache::getNthDocumentID(offset n) {
	LocalLock lock(this);
	char *result = NULL;

	if ((n >= 0) && (n < documentCount)) {
		int whichBucket = n / IDS_PER_BUCKET;
		int whichDocument = n % IDS_PER_BUCKET;
		byte *buffer = NULL;
		if (whichBucket < bucketCount) {
			buffer = mruBucketData;
			if (whichBucket != mruBucket) {
				loadBucket(whichBucket);
				buffer = mruBucketData;
			}
		}
		else if (whichDocument < currentBucketSize)
			buffer = (byte*)currentBucket;
		if (buffer != NULL) {
			offset lastDocumentStart = 0;
			for (int pos = 0; ; pos++) {
				offset delta = 0;
				int shift = 0;
				while (buffer[pos] >= 128) {
					delta = (buffer[pos++] & 127);
					lastDocumentStart += (delta << shift);
					shift += 7;
				}
				delta = buffer[pos++];
				lastDocumentStart += (delta << shift);
				if (--whichDocument < 0) {
					result = duplicateString((char*)&buffer[pos]);
					break;
				}
				while (buffer[pos] != 0)
					pos++;
			} // end for (int pos = 0; ; pos++)
		}
	} // end if ((n >= 0) && (n < documentCount))

	return result;
} // end of getNthDocumentID(offset)


void DocIdCache::loadBucket(int whichBucket) {
	if ((whichBucket < 0) || (whichBucket >= bucketCount))
		return;
	if (mruBucketData != NULL)
		free(mruBucketData);
	int uncompressedBufferSize = (MAX_DOCID_LEN + 8) * IDS_PER_BUCKET;
	byte *buffer = typed_malloc(byte, uncompressedBufferSize);
	int uncompressedSize = -1;
#if USE_ZLIB
	uLongf destLen = (uLongf)uncompressedBufferSize;
	if (uncompress((Bytef*)buffer, &destLen, (Bytef*)docIdBuckets[whichBucket],
				bucketSize[whichBucket]) == Z_OK)
		uncompressedSize = (int)destLen;
#else
	decompressLZW((byte*)docIdBuckets[whichBucket], buffer, &uncompressedSize,
			uncompressedBufferSize);
#endif
	if (uncompressedSize < 0) {
		log(LOG_ERROR, LOG_ID, "Unable to decompress docid buffer.");
		assert(false);
	}
	mruBucketData = buffer;
	mruBucket = whichBucket;
	mruBucketSize = uncompressedSize;
	positionInMruBucket = -1;
	offsetOfMruDocument = -1;
} // end of loadBucket(int)


static void addToNewDocIdCache(byte *buffer, int cnt, ExtentList *files, DocIdCache *target) {
	offset currentFileStart = -1, currentFileEnd = -1;
	offset lastDocumentStart = 0;
	int pos = 0;
	while (--cnt >= 0) {
		offset delta = 0;
		int shift = 0;
		while (buffer[pos] >= 128) {
			delta = (buffer[pos++] & 127);
			lastDocumentStart += (delta << shift);
			shift += 7;
		}
		delta = buffer[pos++];
		lastDocumentStart += (delta << shift);
		if (lastDocumentStart > currentFileEnd)
			if (!files->getFirstEndBiggerEq(lastDocumentStart, &currentFileStart, &currentFileEnd))
				break;
		if (lastDocumentStart >= currentFileStart)
			target->addDocumentID(lastDocumentStart, (char*)&buffer[pos]);
		while (buffer[pos] != 0)
			pos++;
		pos++;
	}
} // end of addToNewDocIdCache(byte*, int, ExtentList*, DocIdCache*)


void DocIdCache::filterAgainstFileList(ExtentList *files) {
	LocalLock lock(this);
	char *oldFileName = duplicateString(fileName);
	char *newFileName = concatenateStrings(fileName, ".temp");

	// create new DocIdCache instance and push all data from this object that lies
	// within the given files into the new object
	DocIdCache *newInstance = new DocIdCache(newFileName, false);
	for (int i = 0; i < bucketCount; i++) {
		loadBucket(i);
		addToNewDocIdCache(mruBucketData, IDS_PER_BUCKET, files, newInstance);
	}
	addToNewDocIdCache((byte*)currentBucket, documentCount % IDS_PER_BUCKET, files, newInstance);
	delete newInstance;
	releaseAllResources();

	fileHandle = open(newFileName, O_RDWR);
	if (fileHandle < 0) {
		snprintf(errorMessage,
				sizeof(errorMessage), "Unable to open file after garbage collection: %s", newFileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		unlink(newFileName);
	}
	else {
		close(fileHandle);
		int error = unlink(oldFileName);
		if (error)
			log(LOG_ERROR, LOG_ID, "Unable to unlink old data file after garbage collection.");
		error = rename(newFileName, oldFileName);
		if (error)
			log(LOG_ERROR, LOG_ID, "Unable to rename .temp file after garbage collection.");
	}

	fileName = duplicateString(oldFileName);
	fileHandle = open(fileName, O_RDWR);
	if (fileHandle < 0)
		log(LOG_ERROR, LOG_ID, "Unable to reopen data file after garbage collection.");
	loadFromDisk();

	free(oldFileName);
	free(newFileName);
} // end of filterAgainstFileList(ExtentList*)


char * DocIdCache::extractID(byte *buffer, offset documentStart, int bufferSize) {
	offset lastDocumentStart = 0;
	int pos = 0;
	char *result = NULL;
	while (pos < bufferSize) {
		offset delta = 0;
		int shift = 0;
		while (buffer[pos] >= 128) {
			delta = (buffer[pos++] & 127);
			lastDocumentStart += (delta << shift);
			shift += 7;
		}
		delta = buffer[pos++];
		lastDocumentStart += (delta << shift);
		if (lastDocumentStart == documentStart)
			result = duplicateString((char*)&buffer[pos]);
		while (buffer[pos++] != 0);
		if (result != NULL) {
			offsetOfMruDocument = lastDocumentStart;
			positionInMruBucket = pos;
			return result;
		}
	}
	return NULL;
} // end of extractID(char*, int)


char * DocIdCache::extractID(byte *buffer, offset documentStart, int bufferSize,
		int positionInMruBucket, offset offsetOfMruDocument) {
	if (documentStart <= offsetOfMruDocument)
		return extractID(buffer, documentStart, bufferSize);
	offset lastDocumentStart = offsetOfMruDocument;
	int pos = positionInMruBucket;
	char *result = NULL;
	while (pos < bufferSize) {
		offset delta = 0;
		int shift = 0;
		while (buffer[pos] >= 128) {
			delta = (buffer[pos++] & 127);
			lastDocumentStart += (delta << shift);
			shift += 7;
		}
		delta = buffer[pos++];
		lastDocumentStart += (delta << shift);
		if (lastDocumentStart == documentStart)
			result = duplicateString((char*)&buffer[pos]);
		while (buffer[pos++] != 0);
		if (result != NULL) {
			offsetOfMruDocument = lastDocumentStart;
			positionInMruBucket = pos;
			return result;
		}
	}
	return NULL;
} // end of extractID(byte*, offset, int, int, offset)


static char *getAllIDs(byte *buffer, int size) {
	char *result = (char*)malloc(size < 0 ? 4 : size + 4);
	int used = 0;
	int pos = 0;
	while (pos < size) {
		while (buffer[pos++] >= 128);
		used += sprintf(&result[used], "%s\n", (char*)&buffer[pos]);
		while (buffer[pos++] != 0);
	}
	return (char*)realloc(result, used + 2);
} // end of getAllIDs(byte*, int)


int DocIdCache::getBucketCount() {
	return (documentCount == 0 ? 0 : bucketCount + 1);
} // end of getBucketCount()


char * DocIdCache::getDocumentIDsInBucket(int whichBucket) {	
	LocalLock lock(this);
	char *result = NULL;
	if ((whichBucket < 0) || (whichBucket > bucketCount))
		return duplicateString("");
	else if (whichBucket == bucketCount)
		return getAllIDs((byte*)currentBucket, currentBucketSize);
	else {
		result = getDocumentID(positions[whichBucket]);
		if (result != NULL)
			free(result);
		result = getAllIDs((byte*)mruBucketData, mruBucketSize);
		return (result != NULL ? result : duplicateString(""));
	}
} // end of getDocumentIDsInBucket(int)



