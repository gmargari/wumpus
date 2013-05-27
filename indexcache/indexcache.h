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
 * The IndexCache class is used to speed up the processing of frequently used
 * queries, such as DOC and DOCNO queries, which appear in BM25 and friends.
 *
 * author: Stefan Buettcher
 * created: 2004-12-28
 * changed: 2007-03-01
 **/


#ifndef __INDEX_TOOLS__INDEXCACHE_H
#define __INDEX_TOOLS__INDEXCACHE_H


#include "cached_extents.h"
#include "../extentlist/extentlist.h"
#include "../misc/configurator.h"
#include "../misc/lockable.h"
#include "../query/gclquery.h"
#include <sys/types.h>
#include <unistd.h>


class Index;


typedef struct {

	/** Normalized query string that produced this list. **/
	char *queryString;

	/** When was this object created? **/
	time_t creationTime;

	/** Number of ExtentList_Cached instances using the data. **/
	int userCount;

	/**
	 * Indicates whether an ExtentList_Cached object should delete this guy in case
	 * it is the last user.
	 **/
	bool deleteUponLastRelease;

	/** Number of extents in this list. **/
	int count;

	/** Uncompressed list of start offsets. **/
	offset *start;

	/** Uncompressed list of end offsets. **/
	offset *end;

	/** Compressed list of extents. **/
	CachedExtents *compressed;

} CachedExtentList;


typedef struct {

	/** Key through which the data in this slot can be accessed. **/
	char key[64];

	/** Size of the data (not including the meta-data). **/
	int size;

	/** When was this slot created? **/
	time_t timeStamp;

	/** The data themselves. **/
	void *data;

	/**
	 * True iff the data stored in this slot may be removed from within
	 * invalidate().
	 **/
	bool mayBeRemovedFromCache;

} CachedCrap;


class IndexCache : public Lockable {

public:

	/**
	 * Number of slots available for cached extent lists. This is the maximum number
	 * of cached lists that can exist at the same time.
	 **/
	static const int CACHE_SLOTS = 8;

	/** Number of slots for the miscellaneous cache. **/
	static const int MISC_CACHE_SLOTS = 8;

	/** Maximum number of elements in a cached extent list. **/
	static const offset MAX_CACHE_LIST_LENGTH = 50 * 1024 * 1024;

private:

	/** The owner of this IndexCache instance. **/
	Index *index;

	bool active;

	CachedExtentList cacheSlots[CACHE_SLOTS];

	CachedCrap crapSlots[MISC_CACHE_SLOTS];

	/** Comma-separated list of cacheable expressions. **/
	char *cacheableExpressions;

	/** Size of the Bloom filter below. **/
	static const unsigned int BLOOM_FILTER_SIZE = 1024;

	/**
	 * This array is used to speed up checking whether a given GCL expression
	 * is cacheable. If bloomFilter[getHashValue(EXPRESSION) % BLOOM_FILTER_SIZE]
	 * is 1, then it might be that the given expression is cacheable. If it
	 * is false, we know for sure that it cannot be cached and return immediately.
	 * This trick is called a "Bloom filter" with k=1 (a single hash function).
	 **/
	char bloomFilter[BLOOM_FILTER_SIZE];

	/** Should extent lists in the cache be kept in compressed form or raw? **/
	bool compressLists;

public:

	/** Creates a new IndexCache instance using the given Index. **/
	IndexCache(Index *index);

	~IndexCache();

	/**
	 * Invalidates all cache contents. To be used after document insertions
	 * or deletions. This method is asynchronous, i.e. the actual task of invalidating
	 * the cache contents is performed by a separate thread.
	 **/
	void invalidate();

	/** Same as above. If you want to avoid deadlocks, NEVER call this method! **/
	void invalidateSynchronously();

	/**
	 * Returns an ExtentList instance that is the result set to the given GCL
	 * query. NULL if the query cannot be found inside the cache.
	 **/
	ExtentList *getCachedList(const char *queryString);

	/**
	 * Tells the cache that a certain list is not used any more. Necessary for
	 * removal of obsolete (after update operations) ExtentList instances.
	 **/
	void deregister(int cacheID);

	/**
	 * In addition to ExtentLists, we can also put arbitrary data into the cache.
	 * These data can later be retrieved using the given "key".
	 **/
	void addMiscDataToCache(const char *key, void *data, int size, bool mayBeRemoved);

	/**
	 * To retrieve miscellaneous data from the cache. Returns true iff the data
	 * for the given "key" have been found and fit into the buffer (buffer size
	 * is given by "maxSize".
	 **/
	bool getMiscDataFromCache(const char *key, void *buffer, int maxSize);

	/**
	 * Returns a pointer to the misc data stored under keyword "key". If there are
	 * no data available, the method returns NULL. "size" will be updated to reflect
	 * the total size of the data stored under the given keyword.
	 **/
	byte *getPointerToMiscDataFromCache(const char *key, int *size);

private:

	/**
	 * Adds the ExtentList for the given GCL query to the set of cached results.
	 * This method requires that "query" is already normalized, i.e. is the output
	 * of GCLQuery::normalizeQueryString(char*). Returns true on success, false
	 * otherwise.
	 **/
	bool addToCache(const char *query);

}; // end of class IndexCache


#endif



