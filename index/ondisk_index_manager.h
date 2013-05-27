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
 * The OnDiskIndexManager class maintains all on-disk indices. It is responsible
 * for all merge operations and decides when the garbage collector is run.
 *
 * author: Stefan Buettcher
 * created: 2005-11-14
 * changed: 2008-01-25
 **/


#ifndef __INDEX__ONDISK_INDEX_MANAGER_H
#define __INDEX__ONDISK_INDEX_MANAGER_H


#include <map>
#include <string>
#include "index_types.h"
#include "index_iterator.h"
#include "ondisk_index.h"
#include "../extentlist/extentlist.h"
#include "../filters/inputstream.h"
#include "../misc/all.h"


class InPlaceIndex;
class CompactIndex;
class Lexicon;


/**
 * This structure is used for garbage collection purposes. For each on-disk index
 * (and for the in-memory index), we keep track of the first and the last posting
 * in the index as well as the number of postings and the number of deleted
 * postings but not yet physically removed postings.
 * ASSERT(firstPosting <= lastPosting)
 * ASSERT(postingCount >= deletedPostingCount)
 **/
struct GarbageInformation {
	long long firstPosting;
	long long lastPosting;
	long long postingCount;
	long long deletedPostingCount;
};


class OnDiskIndexManager : public Lockable {

	friend class Index;

public:

	/**
	 * Sub-index merging strategies:
	 *  - STRATEGY_NO_MERGE: never merge on-disk indices
	 *  - STRATEGY_IMMEDIATE_MERGE: always only keep a single on-disk index
	 *  - STRATEGY_LOG_MERGE: geometric partitioning (base = 2)
	 *  - STRATEGY_SQRT_MERGE: two on-disk indices of size N and sqrt(N)
	 *  - STRATEGY_SMALL_MERGE: merges all on-disk indices that are smaller than 0.5 * MAX_UPDATE_SPACE
	 **/
	static const int STRATEGY_NO_MERGE = 1;
	static const int STRATEGY_IMMEDIATE_MERGE = 2;
	static const int STRATEGY_LOG_MERGE = 4;
	static const int STRATEGY_SQRT_MERGE = 8;
	static const int STRATEGY_SMALL_MERGE = 16;
	static const int STRATEGY_INPLACE = 32;

	/**
	 * If this flag is set in the "mergeStrategy" variable, a hybrid strategy
	 * is employed, using InPlaceIndex to deal with long lists.
	 **/
	static const int STRATEGY_HYBRID = 128;

	/** Task IDs for several index maintenance tasks. **/
	static const int MAINTENANCE_TASK_BUILD_INDEX = 1;
	static const int MAINTENANCE_TASK_MERGE = 2;
	static const int MAINTENANCE_TASK_GC = 3;

	/**
	 * If the combined size of all on-disk indices managed is smaller than this,
	 * the garbage collector will not be run.
	 **/
	static const int MIN_SIZE_FOR_GARBAGE_COLLECTION = 256 * 1024;

	/** Total size for all read buffers in a sub-index merge process. **/
	static const int TOTAL_MERGE_BUFFER_SIZE = 32 * 1024 * 1024;

	/**
	 * Maximum number of Query instances accessing the indices inside the
	 * IndexManager at the same time.
	 **/
	static const int MAX_USER_COUNT = 16;

	/** Maximum number of on-disk indices. **/
	static const int MAX_INDEX_COUNT = 1000;

protected:

// ----- GENERAL-PURPOSE AND CONFIGURATION VARIABLES -----

	/** Our owner. **/
	Index *index;

	/** One of the above strategies. **/
	int mergeStrategy;

	/**
	 * Tells us whether index maintenance operations (merge, garbage collection)
	 * are performed synchronously or asynchronously.
	 **/
	bool asyncIndexMaintenance;

	/** If set to true, forces all on-disk indices to be merged upon exit. **/
	bool mergeAtExit;

	/** Tells us whether the shutdown sequence has been initiated or not. **/
	bool shutdownInitiated;

	/** Has the class destructor been called? **/
	bool destructorCalled;

// ----- USER/QUERY MANAGEMENT: REGISTRATION, DEREGISTRATION -----

	/** List of registered users (timeStamp values). **/
	int64_t userList[MAX_USER_COUNT + 2];

	/** Number of registered users. **/
	int userCount;

	/** Semaphore granting access to the index manager (capacity: MAX_USER_COUNT). **/
	sem_t userSemaphore;

	/**
	 * Current time stamp. Used to determine whether the old or the new index set
	 * has to be used to process a given query.
	 **/
	int64_t currentTimeStamp;

// ----- MANAGEMENT OF ASYNCHRONOUS MAINTENANCE TASKS -----

	/** Tells us whether we currently have a maintenance task running. **/
	bool maintenanceTaskIsRunning;

	/** How many maintenance tasks are currently waiting to be executed? **/
	int maintenanceTaskWaitCnt;

	/** Mutex for asynchronous index maintenance. **/
	sem_t maintenanceTaskSemaphore;

// ----- GARBAGE COLLECTION MANAGEMENT -----

	/**
	 * Total number of postings, total number of deleted postings. Beware! Both
	 * fellows are only approximations of the true values. Should be close, though.
	 **/
	offset postingCount, deletedPostingCount;

	/**
	 * If the relative number of garbage postings in the index exceeds this
	 * threshold, we run the garbage collector.
	 **/
	double garbageThreshold;

	/**
	 * When we have to do a merge operation anyway, we check whether the
	 * relative amount of garbage postings exceeds this second threshold. If so,
	 * we integrate the GC into the merge operation.
	 **/
	double onTheFlyGarbageThreshold;

	/**
	 * This map provides us with information about the number of postings
	 * (approximated) and the number of deleted postings (also approximated) in
	 * each on-disk inverted file.
	 **/
	std::map<std::string, GarbageInformation> *indexList;

// ----- ON-DISK INDICES AND IN-MEMORY INDEX MANAGEMENT -----

	/**
	 * In-memory index holding all postings belonging to recent update operations.
	 * The maximum size of this index is defined by "updateMemoryLimit".
	 **/
	Lexicon *updateIndex;

	/** Maximum allowable size for the in-memory index holding updates. **/
	int updateMemoryLimit;

	/**
	 * If a hybrid strategy was selected, this variable contains the threshold value.
	 * Whenever a posting list in the merge-maintained part of the index exceeds this
	 * threshold (number of postings > inPlaceLimit), it is transferred to the in-place
	 * part of the index.
	 **/
	int inPlaceLimit;

	/** Pointers to the current set of on-disk indices. **/
	CompactIndex **currentIndices;

	/** Number of current indices. **/
	int currentIndexCount;

	char currentIndexMap[1000];

	/** Pointers to the new set of on-disk indices. **/
	CompactIndex **newIndices;

	/** Number of new indices. **/
	int newIndexCount;

	char newIndexMap[1000];

	int64_t newIndexTimeStamp;

	/** Current index for long lists. **/
	InPlaceIndex *currentLongListIndex;

	/** New index for long lists. **/
	InPlaceIndex *newLongListIndex;

	/**
	 * This variable gets set if we are using in-place update with partial flushing,
	 * and the last partial flush reduced memory consumption by less than 15%. In
	 * that case, we force a complete flush when we run out of memory the next time.
	 **/
	bool lastPartialFlushWasPointless;

// ----- END OF MEMBER VARIABLES -----

public:

	OnDiskIndexManager(Index *index);

	~OnDiskIndexManager();

	/** Batched update for sequences of (term,posting) pairs. **/
	void addPostings(char **terms, offset *postings, int count);

	/**
	 * Similar to addPostings(char**, ...) above. Adds a number of postings
	 * for the same term.
	 **/
	void addPostings(char *term, offset *postings, int count);

	/** Same as above, but different. :-) **/
	void addPostings(InputToken *terms, int count);
	
	void triggerGarbageCollection();

	/**
	 * Returns the posting list for the given term, assembled from the
	 * individual posting lists in the on-disk sub-indices managed.
	 **/
	ExtentList *getPostings(const char *term);

	/**
	 * As above, but lets the user choose whether he wants the postings to come
	 * from the on-disk indices, from the in-memory index, or both.
	 **/
	ExtentList *getPostings(const char *term, bool fromDisk, bool fromMemory);

	/**
	 * Same as above, but for the case where we want to fetch multiple lists in
	 * a single call. Allows the index manager to make better use of its knowledge
	 * of on-disk index structures (disk access scheduling).
	 **/
	void getPostings(char **terms, int termCount,
			bool fromDisk, bool fromMemory, ExtentList **results);

// ----- INDEX MAINTENANCE METHODS -----

	/** Builds a new on-disk index from the in-memory data. **/
	void buildNewIndex();

	/** Runs the garbage collector. **/
	void runGC();

	/**
	 * Flushes data in current in-memory index to disk. Triggers merge
	 * operation if appropriate.
	 **/
	void sync();

	/** Merges indices according to the current merge strategy. **/
	void mergeIndicesIfNecessary();

	void deleteOldIndexFiles_SYNC(void *data);

	/**
	 * Stores lower and upper bounds on the total size of the dictionary (number
	 * of terms) in the given variables.
	 **/
	void getDictionarySize(offset *lower, offset *upper);

	/**
	 * Registers a user (Query instance) with the index manager. Returns a user ID
	 * or -1 if registration is not possible (shutdown sequence initiated).
	 **/
	int64_t registerUser(int64_t suggestedID);

	/** Deregisters the user (Query) with the given ID. **/
	void deregisterUser(int64_t userID);

	void startMaintenanceTask();

	void endMaintenanceTask();

	void runMaintenanceTaskSynchronously(int taskID);

	void runMaintenanceTaskAsynchronously(int taskID);

	void checkVMT();

protected:

	/**
	 * Same as in Index class. Only called from there, to propagate garbage collection
	 * information needed for on-the-fly garbage collection.
	 **/
	void notifyOfAddressSpaceChange(int signum, offset start, offset end);

private:

	/** Loads a list of all active on-disk indices into memory. **/
	void loadOnDiskIndices();

	/** Pendant to the above method. **/
	void saveOnDiskIndices();

	static void addNonEmptyExtentList(ExtentList **lists, ExtentList *list, int *count);

	/**
	 * Creates an IndexIterator or MultipleIndexIterator object from the sub-indices
	 * specified by "includeMap" and "includeUpdateIndex".
	 **/
	IndexIterator *createIterator(bool *includeMap, bool includeUpdateIndex);

	/**
	 * This method updates the "I appear in sub-index k" bitmasks in the given
	 * long-list index. The bitmask is necessary to determine whether a given
	 * term may beadded to the long-list index in a merge operation or not (may
	 * only be added if it does not appear in any merge-updated sub-index).
	 * "newFlag" will return the bit-flag value for the target index of the merge
	 * operation.
	 **/
	void updateBitMasks(bool *includeMap, InPlaceIndex *longListIndex, int *newFlag);

	/**
	 * Returns the full path to the file with represents the on-disk index with the
	 * given ID, e.g. "database/index.123".
	 **/
	char *createFileName(int id);

	int findFirstFreeID(int fromWhere);

	int findHighestUsedID();

	void runBuildTask();

	/**
	 * After a merge operation or the execution of the garbage collector, this method
	 * is used to actually replace the old index set by the new one. It automatically
	 * deletes all index files that are no longer needed.
	 **/
	void activateNewIndices();

	/**
	 * This method performs a merge operation with N input indices (managed by the
	 * "iterator" parameter) and 1 or 2 output indices (depending on whether
	 * "longListTarget" is NULL or not). The argument "withGC" is used to specify
	 * whether the garbage collector should be integrated into the merge process.
	 **/
	void doMerge(IndexIterator *iterator, OnDiskIndex *target, InPlaceIndex *longListTarget,
			bool withGC = false, bool mayAddNewTermsToLong = false, int newFlag = 0);

	void computeIndexSetForMergeOperation(
			int mergeStrategy, bool *includeInMerge, bool *includeUpdateIndex, int *indicesInvolved);

	/**
	 * Deleted the in-memory index used to buffer updates. Adjusts the garbage
	 * information for the update index stored in (*indexList)["mem"].
	 **/
	void clearUpdateIndex();

}; // end of class OnDiskIndexManager


typedef struct {

	/** List of old index files that have to be deleted. **/
	char *toDelete[1000];

	/** Number of files in the list. **/
	int toDeleteCount;

	/** The manager that the index files belong to. **/
	OnDiskIndexManager *indexManager;

} ScheduledForDeletion;


#endif


