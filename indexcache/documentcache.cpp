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
 * Implementation of the DocumentCache class.
 *
 * author: Stefan Buettcher
 * created: 2005-06-30
 * changed: 2009-02-01
 **/


#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "documentcache.h"
#include "../misc/all.h"


static const char *LOG_ID = "DocumentCache";


DocumentCache::DocumentCache(char *directory) {
	for (int i = 0; i <= FilteredInputStream::MAX_DOCUMENT_TYPE; i++)
		isCacheable[i] = false;
	isCacheable[FilteredInputStream::DOCUMENT_TYPE_OFFICE] = true;
	isCacheable[FilteredInputStream::DOCUMENT_TYPE_PDF] = true;
	isCacheable[FilteredInputStream::DOCUMENT_TYPE_PS] = true;

	this->directory = duplicateString(directory);
	maxFileCount = 500;
	maxTotalSize = 5000000;

	bool createNewCache = false;
	struct stat buf;
	if (stat(directory, &buf) != 0)
		createNewCache = true;
	else if (!S_ISDIR(buf.st_mode)) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Cannot create document cache directory \"%s\". An object with that name already exists.", directory);
		log(LOG_ERROR, LOG_ID, errorMessage);
		exit(1);
	}

	char *cacheMasterFile = evaluateRelativePathName(directory, "cache.dat");
	if (stat(cacheMasterFile, &buf) != 0)
		createNewCache = true;
	else if (!S_ISREG(buf.st_mode)) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Document cache master file is not a file: \"%s\".", cacheMasterFile);
		log(LOG_ERROR, LOG_ID, errorMessage);
		free(cacheMasterFile);
		exit(1);
	}

	descriptors = typed_malloc(CachedDocumentDescriptor, maxFileCount);
	memset(descriptors, 0, maxFileCount * sizeof(CachedDocumentDescriptor));
	fileCount = 0;
	totalSize = 0;
	for (int i = 0; i < maxFileCount; i++)
		descriptors[i].compressedSize = -1;
	if (createNewCache)
		mkdir(directory, DEFAULT_FILE_PERMISSIONS | S_IXUSR | S_IXGRP);
	else {
		int fd = open(cacheMasterFile, O_RDONLY);
		if (fd < 0) {
			log(LOG_ERROR, LOG_ID, "Unable to open cache.dat file. Creating new cache.");
			fileCount = 0;
			totalSize = 0;
			for (int i = 0; i < maxFileCount; i++)
				descriptors[i].compressedSize = -1;
		}
		else
			loadFromDisk();
	} // end else [!createNewCache]

	free(cacheMasterFile);
	saveToDisk();
} // end of DocumentCache(char*)


DocumentCache::~DocumentCache() {
	saveToDisk();
	free(descriptors);
	free(directory);
} // end of ~DocumentCache()


void DocumentCache::loadFromDisk() {
	char *fileName = evaluateRelativePathName(directory, "cache.dat");
	int fd = open(fileName, O_RDWR | O_CREAT, DEFAULT_FILE_PERMISSIONS);
	free(fileName);
	if (fd < 0)
		log(LOG_ERROR, LOG_ID, "Unable to read cache.dat file from disk.");
	else {
		flock(fd, LOCK_EX);
		forced_ftruncate(fd, (off_t)0);
		forced_read(fd, descriptors, maxFileCount * sizeof(CachedDocumentDescriptor));
		flock(fd, LOCK_UN);
		close(fd);
	}
} // end of loadFromDisk()


void DocumentCache::saveToDisk() {
	char *fileName = evaluateRelativePathName(directory, "cache.dat");
	int fd = open(fileName, O_RDWR | O_CREAT, DEFAULT_FILE_PERMISSIONS);
	free(fileName);
	if (fd < 0)
		log(LOG_ERROR, LOG_ID, "Unable to write cache.dat file to disk.");
	else {
		flock(fd, LOCK_EX);
		forced_ftruncate(fd, (off_t)0);
		forced_write(fd, descriptors, maxFileCount * sizeof(CachedDocumentDescriptor));
		flock(fd, LOCK_UN);
		close(fd);
	}
} // end of saveToDisk()


void DocumentCache::addDocumentTextFromFile(char *filePath, int documentType) {
	if ((documentType < 0) || (documentType > FilteredInputStream::MAX_DOCUMENT_TYPE))
		return;
	if (!isCacheable[documentType])
		return;
} // end of addDocumentTextFromFile(char*, int)


void DocumentCache::addDocumentTextFromFile(
		char *filePath, char *plainTextFile, int conversionTime) {
	snprintf(errorMessage, sizeof(errorMessage), "addDocumentTextFromFile(%s)", filePath);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	struct stat buf;
	if (conversionTime < MIN_CONVERSION_TIME)
		return;
	if (stat(filePath, &buf) != 0)
		return;
	if (stat(plainTextFile, &buf) != 0)
		return;
	if (buf.st_size > MAX_CACHEABLE_FILESIZE)
		return;
	int fd = open(plainTextFile, O_RDONLY);
	if (fd < 0)
		return;
	char *buffer = (char*)malloc(buf.st_size + 1);
	int bufferSize = forced_read(fd, buffer, buf.st_size);
	close(fd);
	addDocumentTextFromMemory(filePath, buffer, bufferSize, conversionTime);
	free(buffer);
} // end of addDocumentTextFromFile(char*, char*, int)


void DocumentCache::addDocumentTextFromMemory(
		char *filePath, char *plainText, int length, int conversionTime) {
	int compressedSize;
	char *buffer;
	if ((length > MAX_CACHEABLE_FILESIZE) || (conversionTime < MIN_CONVERSION_TIME))
		return;
	struct stat buf;
	if (stat(filePath, &buf) != 0)
		return;

	// lock the cache and obtain fresh copy from disk
	LocalLock lock(this);
	loadFromDisk();

	// find free cache slot
	for (int i = 0; i < maxFileCount; i++)
		if (descriptors[i].iNodeID == buf.st_ino)
			if (descriptors[i].compressedSize >= 0)
				evict(i);
	int slot = findFreeCacheSlot(true, length);
	if (slot < 0)
		return;

	// update cache slot
	descriptors[slot].conversionTime = conversionTime;
	descriptors[slot].iNodeID = buf.st_ino;
	descriptors[slot].timeStamp = time(NULL);
	descriptors[slot].timeStamp2 = currentTimeStamp++;
	buffer = compressBuffer(plainText, length, &compressedSize);
	if (compressedSize > 0) {
		descriptors[slot].compressedSize = compressedSize;
		randomFileName(descriptors[slot].fileName, 7);
		saveToFile(descriptors[slot].fileName, buffer, compressedSize);
	}
	else
		descriptors[slot].compressedSize = -1;
	free(buffer);

	// write modified cache data back to disk
	saveToDisk();
} // end of addDocumentTextFromMemory(char*, char*, int, int)


void DocumentCache::randomFileName(char *buffer, int length) {
	LocalLock lock(this);
	while (true) {
		for (int i = 0; i < length; i++)
			buffer[i] = (char)('a' + random() % 26);
		buffer[length + 1] = 0;
		char *fileName = evaluateRelativePathName(directory, buffer);
		struct stat buf;
		if (stat(fileName, &buf) != 0) {
			free(fileName);
			break;
		}
		free(fileName);
	}
} // end of randomFileName(char*, int)


void DocumentCache::saveToFile(char *fileName, char *buffer, int size) {
	fileName = evaluateRelativePathName(directory, fileName);
	int fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_FILE_PERMISSIONS);
	free(fileName);
	if (fd < 0)
		return;
	forced_write(fd, buffer, size);
	close(fd);
} // end of saveToFile(char*, char*, int)


char * DocumentCache::getDocumentText(char *filePath, int *size) {
	snprintf(errorMessage, sizeof(errorMessage), "getDocumentText(%s)", filePath);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	struct stat buf;
	if (stat(filePath, &buf) != 0)
		return NULL;
	return getDocumentText(buf.st_ino, size);
} // end of getDocumentText(char*, int*)


char * DocumentCache::getDocumentText(ino_t iNode, int *size) {
	LocalLock lock(this);
	loadFromDisk();

	for (int i = 0; i < maxFileCount; i++) {
		if (descriptors[i].compressedSize > 0) {
			if (descriptors[i].iNodeID == iNode) {
				descriptors[i].timeStamp = time(NULL);
				descriptors[i].timeStamp2 = currentTimeStamp++;
				char *fileName = evaluateRelativePathName(directory, descriptors[i].fileName);
				int fd = open(fileName, O_RDONLY);
				free(fileName);
				if (fd < 0) {
					evict(i);
					return NULL;
				}
				char *buffer = (char*)malloc(descriptors[i].compressedSize);
				int bufferSize = forced_read(fd, buffer, descriptors[i].compressedSize);
				close(fd);
				if (bufferSize != descriptors[i].compressedSize) {
					evict(i);
					return NULL;
				}
				char *result = decompressBuffer(buffer, bufferSize, size);
				free(buffer);
				return result;
			}
		}
	} // end for (int i = 0; i < maxFileCount; i++)

	return NULL;
} // end of getDocumentText(ino_t, int*)


int DocumentCache::findFreeCacheSlot(bool mayEvict, int size) {
	if (size > MAX_CACHEABLE_FILESIZE)
		return -1;
	LocalLock lock(this);

	if ((fileCount < maxFileCount) && (totalSize + size <= maxTotalSize)) {
		for (int i = 0; i < maxFileCount; i++)
			if (descriptors[i].compressedSize < 0)
				return i;
	}
	else if (mayEvict) {
		evictDocument();
		while (totalSize + size > maxTotalSize)
			evictDocument();
		for (int i = 0; i < maxFileCount; i++)
			if (descriptors[i].compressedSize < 0)
				return i;
	}

	return -1;
} // end of findFreeCacheSlot(bool, int)


void DocumentCache::evictDocument() {
	if (fileCount <= 0)
		return;
	int candidate = -1;
	double candidateScore;
	for (int i = 0; i < maxFileCount; i++)
		if (descriptors[i].compressedSize >= 0) {
			if (candidate < 0)
				candidate = i;
			else {
				double timeElapsed = time(NULL) - descriptors[i].timeStamp + 1;
				double conversionTime = descriptors[i].conversionTime;
				double score =
					timeElapsed / conversionTime * (1 + log(descriptors[i].compressedSize + 1));
				if (score > candidateScore) {
					candidate = i;
					candidateScore = score;
				}
			}
		}
	assert(candidate >= 0);
	evict(candidate);
} // end of evictDocument()


void DocumentCache::evict(int id) {
	LocalLock lock(this);
	assert(descriptors[id].compressedSize >= 0);

	char *fileName = evaluateRelativePathName(directory, descriptors[id].fileName);
	unlink(fileName);
	free(fileName);
	totalSize -= descriptors[id].compressedSize;
	fileCount--;
	descriptors[id].compressedSize = -1;
} // end of evict(int)


char * DocumentCache::fillBufferFromFile(int fd, int *size) {
	if (fd < 0) {
		*size = 0;
		return duplicateString("");
	}
	int allocated = 0, used = 0, result = 0;
	char *buffer = NULL;
	do {
		used += result;
		if (allocated <= used + 256) {
			allocated = MAX((int)(allocated * 1.41), allocated + 16384);
			buffer = (char*)realloc(buffer, allocated);
		}
		result = forced_read(fd, &buffer[used], 256);
	} while (result > 0);
	if (used < allocated * 0.9)
		buffer = (char*)realloc(buffer, used + 1);
	*size = used;
	return buffer;
} // end of fillBufferFromFile(int, int*)


void DocumentCache::fillFileFromBuffer(int fd, const char *buffer, int size) {
	if (fd < 0)
		return;
	while (size > 0) {
		int result = forced_write(fd, buffer, MIN(size, 256));
		if (result <= 0)
			break;
		size -= result;
		buffer = &buffer[result];
	}
} // end of fillFileFromBuffer(int, char*, int)


char * DocumentCache::compressBuffer(const char *buffer, int inputSize, int *outputSize) {
	return compressDecompress("-2", buffer, inputSize, outputSize);
}


char * DocumentCache::decompressBuffer(const char *buffer, int inputSize, int *outputSize) {
	return compressDecompress("-d", buffer, inputSize, outputSize);
}


char * DocumentCache::compressDecompress(const char *param, const char *buffer, int inputSize, int *outputSize) {
	int toCompressorPipe[2];
	if (pipe(toCompressorPipe) != 0) {
		*outputSize = 0;
		return duplicateString("");
	}
	int fromCompressorPipe[2];
	if (pipe(fromCompressorPipe) != 0) {
		close(toCompressorPipe[0]);
		close(toCompressorPipe[1]);
		*outputSize = 0;
		return duplicateString("");
	}
	pid_t zipProcess = fork();
	if (zipProcess == 0) {
		dup2(toCompressorPipe[0], fileno(stdin));
		dup2(fromCompressorPipe[1], fileno(stdout));
		close(toCompressorPipe[1]);
		execlp("gzip", "gzip", "-c", param, NULL);
		close(fromCompressorPipe[0]);
		exit(1);
	}
	close(toCompressorPipe[0]);
	close(fromCompressorPipe[1]);
	pid_t feedProcess = fork();
	if (feedProcess == 0) {
		close(fromCompressorPipe[0]);
		fillFileFromBuffer(toCompressorPipe[1], buffer, inputSize);
		close(toCompressorPipe[1]);
		exit(0);
	}
	close(toCompressorPipe[1]);
	char *result = fillBufferFromFile(fromCompressorPipe[0], outputSize);
	close(fromCompressorPipe[0]);
	waitpid(feedProcess, NULL, 0);
	waitpid(zipProcess, NULL, 0);
	return result;
} // end of compressDecompress(char*, char*, int, int*)




