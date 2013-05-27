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
 * author: Stefan Buettcher
 * created: 2004-12-28
 * changed: 2009-02-01
 **/


#include <stdio.h>
#include <string.h>
#include "indexcache.h"
#include "extentlist_cached.h"
#include "extentlist_cached_compressed.h"
#include "../index/index.h"
#include "../misc/all.h"
#include "../query/gclquery.h"
#include "../query/querytokenizer.h"


static const char * LOG_ID = "IndexCache";


IndexCache::IndexCache(Index *index) {
	// initialize cache slots
	this->index = index;
	for (int i = 0; i < CACHE_SLOTS; i++) {
		cacheSlots[i].queryString = NULL;
		cacheSlots[i].creationTime = (time_t)0;
	}
	for (int i = 0; i < MISC_CACHE_SLOTS; i++) {
		crapSlots[i].data = NULL;
		crapSlots[i].size = -1;
	}
	active = true;

	// initialize Bloom filter from configuration data; create copy of the list
	// of cacheable expressions, with all expressions already normalized; this
	// will save us some time when comparing queries with cacheable expressions
	memset(bloomFilter, 1, sizeof(bloomFilter));
	char cachedExpressions[MAX_CONFIG_VALUE_LENGTH];
	if (getConfigurationValue("CACHED_EXPRESSIONS", cachedExpressions)) {
		cacheableExpressions = duplicateString("");
		QueryTokenizer *tok = new QueryTokenizer(cachedExpressions);
		while (tok->hasNext()) {
			char *normalized = GCLQuery::normalizeQueryString(tok->getNext());
			bloomFilter[simpleHashFunction(normalized) % BLOOM_FILTER_SIZE] = 1;
			char *temp = concatenateStrings(cacheableExpressions, ",", normalized);
			free(cacheableExpressions);
			cacheableExpressions = temp;
			free(normalized);
		}
		delete tok;
	}
	else
		cacheableExpressions = NULL;

	// fetch from config whether we want to compress lists in the cache
	getConfigurationBool("COMPRESSED_INDEXCACHE", &compressLists, false);
} // end of IndexCache(Index*)


static void freeCacheSlot(CachedExtentList *slot) {
	if (slot->queryString != NULL) {
		free(slot->queryString);
		slot->queryString = NULL;
	}
	if (slot->start != NULL) {
		free(slot->start);
		slot->start = NULL;
	}
	if (slot->end != NULL) {
		free(slot->end);
		slot->end = NULL;
	}
	if (slot->compressed != NULL) {
		freeCachedExtents(slot->compressed);
		slot->compressed = NULL;
	}
	slot->creationTime = (time_t)0;
} // end of freeCacheSlot(CachedExtentList*)


IndexCache::~IndexCache() {
	LocalLock lock(this);
	active = false;
	for (int i = 0; i < CACHE_SLOTS; i++)
		if (cacheSlots[i].queryString != NULL)
			freeCacheSlot(&cacheSlots[i]);
	for (int i = 0; i < MISC_CACHE_SLOTS; i++)
		if (crapSlots[i].size >= 0) {
			free(crapSlots[i].data);
			crapSlots[i].size = -1;
		}
	if (cacheableExpressions != NULL) {
		free(cacheableExpressions);
		cacheableExpressions = NULL;
	}
} // end of ~IndexCache()


bool IndexCache::addToCache(const char *query) {
	LocalLock lock(this);
	if (query == NULL)
		return false;
	if (*query == 0)
		return false;

	GCLQuery *q = NULL;
	int slot = -1;

	// check whether the list is already in the cache
	for (int i = 0; i < CACHE_SLOTS; i++)
		if (cacheSlots[i].queryString != NULL)
			if (strcmp(cacheSlots[i].queryString, query) == 0) {
				snprintf(errorMessage, sizeof(errorMessage), "addToCache: already in cache: %s", query);
				log(LOG_DEBUG, LOG_ID, errorMessage);
				return false;
			}

	// search for free cache slot
	for (int i = 0; i < CACHE_SLOTS; i++)
		if (cacheSlots[i].queryString == NULL) {
			slot = i;
			break;
		}

	// if no free slot has been found, there is not much we can do...
	if (slot < 0)
		return false;

	// Create list by parsing the query string. Make sure that the underlying
	// GCLQuery instance does not use the cache (would lead to an infinite loop)
	// and only fetches postings from the on-disk indices (allows us to update
	// the cache less frequently and instead apply small patches at query time).
	const char *modifiers[3] = { "nocache", "disk_only", NULL };
	q = new GCLQuery(index, "gcl", modifiers, query, Index::SUPERUSER, Query::DEFAULT_MEMORY_LIMIT);
	if (!q->parse()) {
		delete q;
		return false;
	}
	else if (q->getResult()->getLength() > MAX_CACHE_LIST_LENGTH) {
		offset length = q->getResult()->getLength();
		snprintf(errorMessage, sizeof(errorMessage),
				"List too long for cache: %s (%lld)", query, (long long)length);
		log(LOG_ERROR, LOG_ID, errorMessage);
		delete q;
		return false;
	}

	// prepare cache slot
	ExtentList *list = q->getResult();
	cacheSlots[slot].queryString = duplicateString(query);
	int count = cacheSlots[slot].count = list->getLength();
	cacheSlots[slot].creationTime = time(NULL);
	cacheSlots[slot].userCount = 0;
	cacheSlots[slot].deleteUponLastRelease = false;

	// fill extent data into cache slot
	if (compressLists) {
		cacheSlots[slot].start = NULL;
		cacheSlots[slot].end = NULL;
		cacheSlots[slot].compressed = createCachedExtents(list);
	}
	else {
		cacheSlots[slot].start = typed_malloc(offset, count + 1);
		cacheSlots[slot].end = typed_malloc(offset, count + 1);
		cacheSlots[slot].compressed = NULL;
		int n = list->getNextN(0, MAX_OFFSET, MAX_INT, cacheSlots[slot].start, cacheSlots[slot].end);
		assert(n == cacheSlots[slot].count);
	}
	delete q;

	snprintf(errorMessage, sizeof(errorMessage), "%s loaded into cache slot %d.", query, slot);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	return true;
} // end of addToCache(char*)


void IndexCache::invalidateSynchronously() {
	LocalLock lock(this);
	log(LOG_DEBUG, LOG_ID, "Invalidating cache content.");
	for (int i = 0; i < CACHE_SLOTS; i++)
		if (cacheSlots[i].queryString != NULL) {
			if (cacheSlots[i].userCount > 0)
				cacheSlots[i].deleteUponLastRelease = true;
			else
				freeCacheSlot(&cacheSlots[i]);
		}
	for (int i = 0; i < MISC_CACHE_SLOTS; i++)
		if ((crapSlots[i].size >= 0) && (crapSlots[i].mayBeRemovedFromCache)) {
			free(crapSlots[i].data);
			crapSlots[i].size = -1;
		}
} // end of invalidateSynchronously()


static void *invalidateCache(void *data) {
	if (data == NULL)
		return NULL;
	IndexCache *cache = (IndexCache*)data;
	cache->invalidateSynchronously();
	return NULL;
} // end of invalidateCache(void*)


void IndexCache::invalidate() {
	// This is really ugly: Because the OnDiskIndexManager can call this function,
	// while at the same time we might be calling the OnDiskManager in order to
	// obtain extent lists when populating the cache, we need this to be asynchronous.
	// Otherwise, we will get a nice deadlock.
	pthread_t thread;
	pthread_create(&thread, NULL, invalidateCache, this);
	pthread_detach(thread);
} // end of invalidate()


ExtentList * IndexCache::getCachedList(const char *queryString) {
	LocalLock lock(this);
	if ((!active) || (queryString == NULL))
		return NULL;
	if (cacheableExpressions == NULL)
		return NULL;

	bool emptySlotFound = false;
	ExtentList *result = NULL;

	// normalize query and check whether Bloom filter says 1
	char *query = GCLQuery::normalizeQueryString(queryString);
	if (*query == 0)
		goto getCachedList_EXIT;
	if (bloomFilter[simpleHashFunction(query) % BLOOM_FILTER_SIZE] == 0)
		goto getCachedList_EXIT;

getCachedList_START:

	for (int i = 0; i < CACHE_SLOTS; i++) {
		if (cacheSlots[i].queryString == NULL)
			emptySlotFound = true;
		else if (strcmp(cacheSlots[i].queryString, query) == 0) {
			snprintf(errorMessage, sizeof(errorMessage), "Fetching list from cache: %s", query);
			log(LOG_DEBUG, LOG_ID, errorMessage);

			// list found in the cache; check whether we may return the list; if it has
			// been marked for deletion after last release, we may not return it because
			// this would delay its deletion indefinitely long
			if (cacheSlots[i].deleteUponLastRelease)
				goto getCachedList_EXIT;

			// fetch postings corresponding to on-disk indices from cache
			CachedExtentList *slot = &cacheSlots[i];
			if (cacheSlots[i].count == 0)
				result = new ExtentList_Empty();
			else {
				slot->userCount++;
				if (cacheSlots[i].compressed != NULL)
					result = new ExtentList_Cached_Compressed(this, i, slot->compressed);
				else
					result = new ExtentList_Cached(this, i, slot->start, slot->end, slot->count);
			}

			// append update postings from in-memory index to existing cached list
			const char *modifiers[3] = { "nocache", "mem_only", NULL };
			GCLQuery q(index, "gcl", modifiers, query, Index::SUPERUSER, Query::DEFAULT_MEMORY_LIMIT);
			if (q.parse()) {
				offset length = q.getResult()->getLength();
				if (length + result->getLength() > MAX_CACHE_LIST_LENGTH) {
					snprintf(errorMessage, sizeof(errorMessage),
							"List too long for cache: %s (%lld+%lld)",
							query, (long long)result->getLength(), (long long)length);
					log(LOG_ERROR, LOG_ID, errorMessage);
				}
				else if (length > 0) {
					offset *start = typed_malloc(offset, length + 1);
					offset *end = typed_malloc(offset, length + 1);
					q.getResult()->getNextN(0, MAX_OFFSET, MAX_INT, start, end);
					if (result->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
						delete result;
						result = new ExtentList_Cached(NULL, -1, start, end, length);
					}
					else {
						ExtentList **lists = typed_malloc(ExtentList*, 2);
						lists[0] = result;
						lists[1] = new ExtentList_Cached(NULL, -1, start, end, length);
						result = new ExtentList_OrderedCombination(lists, 2);
					}
				}
			}
		}
	} // end for (int i = 0; i < CACHE_SLOTS; i++)

	if (result == NULL) {
		if (emptySlotFound) {
			QueryTokenizer tok(cacheableExpressions);
			while (tok.hasNext()) {
				char *token = tok.getNext();
				if (strcmp(token, query) == 0) {
					if (!addToCache(query)) {
						// try to add to cache; if not possible (maybe because list is too long),
						// return uncached expression instead
						const char *modifiers[2] = { "nocache", NULL };
						GCLQuery q(index, "gcl", modifiers, query, Index::SUPERUSER, Query::DEFAULT_MEMORY_LIMIT);
						if (!q.parse())
							result = new ExtentList_Empty();
						else {
							result = q.getResult();
							q.resultList = NULL;
						}
						free(query);
						return result;
					}
					emptySlotFound = false;
					goto getCachedList_START;
				}
			}
		}
		else {
			int toDelete = -1;
			for (int i = 0; i < CACHE_SLOTS; i++)
				if (cacheSlots[i].userCount == 0) {
					if (toDelete < 0)
						toDelete = i;
					else if (cacheSlots[i].creationTime < cacheSlots[toDelete].creationTime)
						toDelete = i;
				}
			if (toDelete >= 0) {
				freeCacheSlot(&cacheSlots[toDelete]);
				goto getCachedList_START;
			}
		}
	} // end if (result == NULL)

getCachedList_EXIT:
	
	free(query);
	return result;
} // end of getCachedList(char*)


void IndexCache::deregister(int cacheID) {
	LocalLock lock(this);
	if (cacheSlots[cacheID].queryString != NULL) {
		if (cacheSlots[cacheID].userCount > 0)
			cacheSlots[cacheID].userCount--;
		if (cacheSlots[cacheID].userCount == 0)
			if (cacheSlots[cacheID].deleteUponLastRelease)
				freeCacheSlot(&cacheSlots[cacheID]);
	}
} // end of deregister(char*)


void IndexCache::addMiscDataToCache(const char *key, void *data, int size, bool mayBeRemoved) {
	LocalLock lock(this);
	if ((strlen(key) >= 64) || (size <= 0))
		return;
	int candidate = -1;
	for (int i = 0; i < MISC_CACHE_SLOTS; i++) {
		if (crapSlots[i].size < 0)
			candidate = i;
		else {		
			if (!crapSlots[i].mayBeRemovedFromCache)
				continue;
			if (candidate < 0)
				candidate = i;
			else if (crapSlots[i].timeStamp < crapSlots[candidate].timeStamp)
				candidate = i;
		}
	}
	if (candidate >= 0) {
		if (crapSlots[candidate].size >= 0)
			free(crapSlots[candidate].data);
		strcpy(crapSlots[candidate].key, key);
		crapSlots[candidate].size = size;
		crapSlots[candidate].data = malloc(size);
		memcpy(crapSlots[candidate].data, data, size);
		crapSlots[candidate].timeStamp = time(NULL);
		crapSlots[candidate].mayBeRemovedFromCache = mayBeRemoved;
	}
} // end of addMiscDataToCache(char*, void*, int)


bool IndexCache::getMiscDataFromCache(const char *key, void *buffer, int maxSize) {
	LocalLock lock(this);
	if ((strlen(key) >= 64) || (maxSize <= 0))
		return false;
	for (int i = 0; i < MISC_CACHE_SLOTS; i++)
		if ((crapSlots[i].size >= 0) && (crapSlots[i].size <= maxSize))
			if (strcmp(key, crapSlots[i].key) == 0) {
				memcpy(buffer, crapSlots[i].data, crapSlots[i].size);
				return true;
			}
	return false;
} // end of getMiscDataFromCache(char*, void*, int)


byte * IndexCache::getPointerToMiscDataFromCache(const char *key, int *size) {
	LocalLock lock(this);
	if (strlen(key) >= 64)
		return NULL;
	byte *result = 0;
	for (int i = 0; i < MISC_CACHE_SLOTS; i++)
		if ((crapSlots[i].size >= 0) && (strcmp(key, crapSlots[i].key) == 0)) {
			*size = crapSlots[i].size;
			return (byte*)crapSlots[i].data;
		}
	return NULL;
} // end of getPointerToMiscDataFromCache(char*, int*)



