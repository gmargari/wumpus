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
 * Implementation of the class FileSystemCache.
 * Documentation can be found in file "filesystem.h".
 *
 * author: Stefan Buettcher
 * created: 2004-09-03
 * changed: 2004-09-30
 **/


#include "filesystem.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../misc/alloc.h"


#define FILESYSTEMCACHE_HASH_SIZE 1847

// switch on/off debugging output for FileSystemCache
//#define FILESYSTEMCACHE_DEBUG


FileSystemCache::FileSystemCache(FileSystem* fs, int pageSize, int pageCount) {
	this->fileSystem = fs;
	this->pageSize = pageSize;
	this->cacheSize = pageCount;

	workMode = FILESYSTEMCACHE_LRU;
	currentPageCount = 0;

	firstSlot = (FileSystemCacheSlot*)malloc(sizeof(FileSystemCacheSlot));
	lastSlot = (FileSystemCacheSlot*)malloc(sizeof(FileSystemCacheSlot));
	firstSlot->pageNumber = -1;
	firstSlot->data = NULL;
	firstSlot->prev = NULL;
	firstSlot->next = lastSlot;
	lastSlot->pageNumber = -2;
	lastSlot->data = NULL;
	lastSlot->prev = firstSlot;
	lastSlot->next = NULL;

	whereIsPage =
		(FileSystemCacheHashElement**)malloc(FILESYSTEMCACHE_HASH_SIZE * sizeof(FileSystemCacheHashElement*));
	for (int i = 0; i < FILESYSTEMCACHE_HASH_SIZE; i++)
		whereIsPage[i] = NULL;
	readWriteBuffer = (byte*)malloc(pageSize);
} // end of FileSystemCache(int, int)


FileSystemCache::~FileSystemCache() {

	// flush cache first so that we don't lose any data
	flush();
	
	// free memory occupied by pageNumber hashtable
	for (int i = 0; i < FILESYSTEMCACHE_HASH_SIZE; i++) {
		FileSystemCacheHashElement *hashRunner = whereIsPage[i];
		while (hashRunner != NULL) {
			FileSystemCacheHashElement *next = (FileSystemCacheHashElement*)hashRunner->next;
			free(hashRunner);
			hashRunner = next;
		}
	}
	free(whereIsPage);

	// free memory occupied by cache data
	FileSystemCacheSlot *slotRunner = firstSlot;
	while (slotRunner != NULL) {
		FileSystemCacheSlot *next = (FileSystemCacheSlot*)slotRunner->next;
		if (slotRunner->data != NULL)
			free(slotRunner->data);
		free(slotRunner);
		slotRunner = next;
	}

	free(readWriteBuffer);
} // end of ~FileSystemCache()


FileSystemCacheSlot* FileSystemCache::findPage(int pageNumber) {
	int hashValue = pageNumber % FILESYSTEMCACHE_HASH_SIZE;
	FileSystemCacheHashElement *hashElement = whereIsPage[hashValue];
	while (hashElement != NULL) {
		if (hashElement->data->pageNumber == pageNumber)
			return hashElement->data;
		hashElement = (FileSystemCacheHashElement*)hashElement->next;
	}
	return NULL;
} // end of findPage(int)


void FileSystemCache::printCacheQueue() {
	FileSystemCacheSlot *slot = firstSlot;
	fprintf(stderr, "[Forward:");
	while (slot != NULL) {
		fprintf(stderr, " %i", slot->pageNumber);
		slot = (FileSystemCacheSlot*)slot->next;
	}
	fprintf(stderr, "] ");
	slot = lastSlot;
	fprintf(stderr, "[Backward:");
	while (slot != NULL) {
		fprintf(stderr, " %i", slot->pageNumber);
		slot = (FileSystemCacheSlot*)slot->prev;
	}
	fprintf(stderr, "]\n");
} // end of printCacheQueue()


bool FileSystemCache::isInCache(int pageNumber) {
	FileSystemCacheSlot *slot = findPage(pageNumber);
#ifdef FILESYSTEMCACHE_DEBUG
	fprintf(stderr, "FileSystemCache::isInCache(%i) returns %s.\n",
			pageNumber, (slot == NULL ? "false" : "true"));
#endif
	if (slot == NULL)
		return false;
	else
		return true;
} // end of isInCache(int)


void FileSystemCache::setWorkMode(int newWorkMode) {
	if ((newWorkMode == FILESYSTEMCACHE_LRU) || (newWorkMode == FILESYSTEMCACHE_FIFO))
		workMode = newWorkMode;
} // end of setWorkMode(int)


void FileSystemCache::touchSlot(FileSystemCacheSlot *slot) {
	// remove slot from old position
	((FileSystemCacheSlot*)slot->prev)->next = slot->next;
	((FileSystemCacheSlot*)slot->next)->prev = slot->prev;

#ifdef FILESYSTEMCACHE_DEBUG
	printCacheQueue();
#endif

	// insert slot at first position (after sentinel)
	slot->next = firstSlot->next;
	((FileSystemCacheSlot*)slot->next)->prev = slot;
	slot->prev = firstSlot;
	firstSlot->next = slot;
} // end of touchSlot(FileSystemCacheSlot*)


int FileSystemCache::getPage(int pageNumber, void *buffer) {
#ifdef FILESYSTEMCACHE_DEBUG
	fprintf(stderr, "FileSystemCache::getPage(%i, ...) called.\n", pageNumber);
#endif
	FileSystemCacheSlot *slot = findPage(pageNumber);
	if (slot == NULL)
		return FILESYSTEM_ERROR;
	memcpy(buffer, slot->data, pageSize);
	if (workMode == FILESYSTEMCACHE_LRU)
		touchSlot(slot);
#ifdef FILESYSTEMCACHE_DEBUG
	printCacheQueue();
#endif
	return FILESYSTEM_SUCCESS;
} // end of getPage(int, void*)


void FileSystemCache::removeHashEntry(int pageNumber) {
	int hashValue = pageNumber % FILESYSTEMCACHE_HASH_SIZE;
	FileSystemCacheHashElement *hashElement = whereIsPage[hashValue];
	if (hashElement == NULL)
		return;

	if (hashElement->data->pageNumber == pageNumber) {
		whereIsPage[hashValue] = (FileSystemCacheHashElement*)hashElement->next;
		free(hashElement);
		return;
	}
	while (hashElement->next != NULL) {
		FileSystemCacheHashElement *next = (FileSystemCacheHashElement*)hashElement->next;
		if (next->data->pageNumber == pageNumber) {
			hashElement->next = next->next;
			free(next);
			return;
		}
		hashElement = next;
	}
} // end of removeHashEntry(int)


void FileSystemCache::evict(FileSystemCacheSlot *toEvict) {
	int pageNumber = toEvict->pageNumber;
#ifdef FILESYSTEMCACHE_DEBUG
	fprintf(stderr, "Evicting page %i from cache.\n", pageNumber);
	if (toEvict->hasBeenChanged)
		fprintf(stderr, "  Page has been modified. Writing content to disk.\n");
#endif
	if (toEvict->hasBeenChanged)
		fileSystem->writePage_UNCACHED(pageNumber, 0, pageSize, toEvict->data);
	free(toEvict->data);
	((FileSystemCacheSlot*)toEvict->prev)->next = toEvict->next;
	((FileSystemCacheSlot*)toEvict->next)->prev = toEvict->prev;
	removeHashEntry(pageNumber);
	free(toEvict);
	currentPageCount--;
} // end of evict(FileSystemCacheSlot*)


FileSystemCacheSlot * FileSystemCache::loadPage(int pageNumber, void *buffer, bool copyData) {
#ifdef FILESYSTEMCACHE_DEBUG
	fprintf(stderr, "FileSystemCache::loadPage(%i, ...) called.\n", pageNumber);
#endif

	// special treatment if the page is currently in the cache
	FileSystemCacheSlot *slot = findPage(pageNumber);
	if (slot != NULL) {
		if (copyData)
			memcpy(slot->data, buffer, pageSize);
		else {
			free(slot->data);
			slot->data = (char*)buffer;
		}
		touchSlot(slot);
		return slot;
	}
	
	// if the cache is full, remove a page
	while (currentPageCount >= cacheSize) {
		FileSystemCacheSlot *toEvict = (FileSystemCacheSlot*)lastSlot->prev;
		bool hasBeenChanged = toEvict->hasBeenChanged;
		int pageNumber = toEvict->pageNumber;
		evict(toEvict);
		if (hasBeenChanged) {
			for (int i = 1; i <= 3; i++) {
				toEvict = findPage(pageNumber + i);
				if (toEvict == NULL)
					break;
				if (!toEvict->hasBeenChanged)
					break;
				evict(toEvict);
			}
		}
	} // end while (currentPageCount >= cacheSize)

	// load new page into cache
	FileSystemCacheSlot *newSlot = (FileSystemCacheSlot*)malloc(sizeof(FileSystemCacheSlot));
	newSlot->pageNumber = pageNumber;
	if (copyData) {
		newSlot->data = (char*)malloc(pageSize);
		memcpy(newSlot->data, buffer, pageSize);
	}
	else
		newSlot->data = (char*)buffer;
	newSlot->hasBeenChanged = false;
	int hashValue = pageNumber % FILESYSTEMCACHE_HASH_SIZE;
	FileSystemCacheHashElement *cacheElement = (FileSystemCacheHashElement*)malloc(sizeof(FileSystemCacheHashElement));
	cacheElement->data = newSlot;
	cacheElement->next = whereIsPage[hashValue];
	whereIsPage[hashValue] = cacheElement;

	// insert as first element (after sentinel)
	newSlot->next = firstSlot->next;
	((FileSystemCacheSlot*)newSlot->next)->prev = newSlot;
	newSlot->prev = firstSlot;
	firstSlot->next = newSlot;
	currentPageCount++;
#ifdef FILESYSTEMCACHE_DEBUG
	printCacheQueue();
#endif
	return newSlot;
} // end of loadPage(int, void*, bool)


int FileSystemCache::touchPage(int pageNumber) {
#ifdef FILESYSTEMCACHE_DEBUG
	fprintf(stderr, "FileSystemCache::touchPage(%i) called.\n", pageNumber);
	printCacheQueue();
#endif
	FileSystemCacheSlot *slot = findPage(pageNumber);
	if (slot == NULL)
		return FILESYSTEM_ERROR;
	touchSlot(slot);
#ifdef FILESYSTEMCACHE_DEBUG
	fprintf(stderr, "After touching:\n");
	printCacheQueue();
#endif
	return FILESYSTEM_SUCCESS;
} // end of touchPage(int)


int FileSystemCache::writeToPage(int pageNumber, int offset, int length, void *buffer) {
	if (pageNumber <= 0)
		return fileSystem->writePage_UNCACHED(pageNumber, offset, length, buffer);
	FileSystemCacheSlot *slot = findPage(pageNumber);
	
	if (slot != NULL) {
		// page found: copy data and set "modified" flag
		memcpy(&slot->data[offset], buffer, length);
		slot->hasBeenChanged = true;
		touchSlot(slot);
		return FILESYSTEM_SUCCESS;
	}

	// if the page is not found, we distinguish between two cases:
	// (a) an entire page is written; here, it is unlikely that there will be
	//     another read/write operation accessing the page in the near future
	// (b) only part of the page is written: cache the page and wait for
	//     further operations on the page

	if (length == pageSize)
		return fileSystem->writePage_UNCACHED(pageNumber, offset, length, buffer);
	else {
		if (fileSystem->readPage_UNCACHED(pageNumber, 0, pageSize, readWriteBuffer) == FILESYSTEM_ERROR)
			return FILESYSTEM_ERROR;
		slot = loadPage(pageNumber, readWriteBuffer, false);
		readWriteBuffer = (byte*)malloc(pageSize);
		memcpy(&slot->data[offset], buffer, length);
		slot->hasBeenChanged = true;
		return FILESYSTEM_SUCCESS;
	}
} // end of writeToPage(int, int, int, void*)


int FileSystemCache::readFromPage(int pageNumber, int offset, int length, void *buffer) {
	if (pageNumber <= 0)
		return fileSystem->readPage_UNCACHED(pageNumber, offset, length, buffer);

	FileSystemCacheSlot *slot = findPage(pageNumber);

	if (slot != NULL) {
		// page found: copy data and set "modified" flag
		memcpy(buffer, &slot->data[offset], length);
		touchSlot(slot);
		return FILESYSTEM_SUCCESS;
	}

	if (fileSystem->readPage_UNCACHED(pageNumber, 0, pageSize, readWriteBuffer) == FILESYSTEM_ERROR)
		return FILESYSTEM_ERROR;
	slot = loadPage(pageNumber, readWriteBuffer, false);
	readWriteBuffer = (byte*)malloc(pageSize);
	memcpy(buffer, &slot->data[offset], length);

	// try to prefetch data	
	for (int i = 1; i <= 3; i++)
		if (pageNumber + i < fileSystem->getPageCount()) {
			slot = findPage(pageNumber + i);
			if (slot == NULL) {
				if (fileSystem->readPage_UNCACHED(pageNumber + i, 0, pageSize, readWriteBuffer)
						== FILESYSTEM_SUCCESS) {
					loadPage(pageNumber + i, readWriteBuffer, false);
					readWriteBuffer = (byte*)malloc(pageSize);
				}
			}
		} // end if (pageNumber + i < fileSystem->getPageCount())
	
return FILESYSTEM_SUCCESS;
} // end of readFromPage(int, int, int, void*)


void FileSystemCache::flush() {
	while (firstSlot->next != lastSlot) {
		FileSystemCacheSlot *toEvict = (FileSystemCacheSlot*)firstSlot->next;
		evict(toEvict);
	}
} // end of flush()


