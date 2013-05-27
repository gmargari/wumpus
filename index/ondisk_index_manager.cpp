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
 * Implementation of the OnDiskIndexManager class.
 *
 * The code in this file is a total mess. Everything related to
 * asynchronous index maintenance is kind of bizarre. We may run into
 * very awkward situations, where a new on-disk index is created while
 * an existing set of indices is being merged into yet another index. In
 * that case, we have to make sure that the two new indices are inserted
 * into the set of active index partitions in the right order. Stuff like
 * this. The index manager is supposed to handle all these crazy situations.
 * I hope it actually works.
 * If you want to be on the safe side, don't use asynchronous index
 * maintenance.
 *
 * author: Stefan Buettcher
 * created: 2005-11-20
 * changed: 2009-02-01
 **/


#include <limits.h>
#include <string.h>
#include "ondisk_index_manager.h"
#include "inplace_index.h"
#include "compactindex.h"
#include "compressed_lexicon.h"
#include "hybrid_lexicon.h"
#include "index.h"
#include "index_merger.h"
#include "multiple_index_iterator.h"
#include "realloc_lexicon.h"
#include "threshold_iterator.h"
#include "../extentlist/simplifier.h"
#include "../misc/alloc.h"
#include "../terabyte/terabyte_lexicon.h"


static const char * LOG_ID = "OnDiskIndexManager";


OnDiskIndexManager::OnDiskIndexManager(Index *index) {
	this->index = index;
	shutdownInitiated = false;
	destructorCalled = false;

	updateIndex = false;
	indexList = new std::map<std::string, GarbageInformation>();
	GarbageInformation gi;
	gi.postingCount = gi.deletedPostingCount = 0;
	(*indexList)["index.mem"] = gi;

	// initialize user and maintenance task counters
	userCount = 0;
	SEM_INIT(userSemaphore, MAX_USER_COUNT);
	maintenanceTaskIsRunning = false;
	maintenanceTaskWaitCnt = 0;
	SEM_INIT(maintenanceTaskSemaphore, 1);

	// get configuration variables
	getConfigurationBool("ASYNC_INDEX_MAINTENANCE", &asyncIndexMaintenance, false);
	getConfigurationInt("MAX_UPDATE_SPACE", &updateMemoryLimit, 32 * 1024 * 1024);
	getConfigurationDouble("GARBAGE_COLLECTION_THRESHOLD", &garbageThreshold, 0.40);
	garbageThreshold = MAX(0.001, MIN(0.999, garbageThreshold));
	getConfigurationDouble("ONTHEFLY_GARBAGE_COLLECTION_THRESHOLD", &onTheFlyGarbageThreshold, 0.25);
	onTheFlyGarbageThreshold = MAX(0.001, MIN(0.999, onTheFlyGarbageThreshold));
	getConfigurationBool("MERGE_AT_EXIT", &mergeAtExit, false);
	char mergeStrategy[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue("UPDATE_STRATEGY", mergeStrategy)) {
		log(LOG_ERROR, LOG_ID, "Configuration variable UPDATE_STRATEGY undefined! Defaulting to IMMEDIATE_MERGE.");
		strcpy(mergeStrategy, "IMMEDIATE_MERGE");
	}


	char lexiconType[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue("LEXICON_TYPE", lexiconType)) {
		log(LOG_ERROR, LOG_ID, "Configuration variable LEXICON_TYPE undefined! Defaulting to COMPRESSED_LEXICON.");
		strcpy(lexiconType, "COMPRESSED_LEXICON");
	}

#if SUPPORT_APPEND_TAQT
	// The transformation-at-query-time implementation of APPEND
	// is incompatible with garbage collection. Disable it.
	garbageThreshold = onTheFlyGarbageThreshold = 2;
#endif
#if SUPPORT_APPEND_TAIT
	// The transformation-at-indexing-time implementation of APPEND
	// is incompatible with on-the-fly garbage collection. Disable it.
	onTheFlyGarbageThreshold = 2;
#endif

	// initialize management information
	currentTimeStamp = 1;
	if (strcasecmp(mergeStrategy, "NO_MERGE") == 0)
		this->mergeStrategy = STRATEGY_NO_MERGE;
	else if (strcasecmp(mergeStrategy, "IMMEDIATE_MERGE") == 0)
		this->mergeStrategy = STRATEGY_IMMEDIATE_MERGE;
	else if (strcasecmp(mergeStrategy, "LOG_MERGE") == 0)
		this->mergeStrategy = STRATEGY_LOG_MERGE;
	else if (strcasecmp(mergeStrategy, "SQRT_MERGE") == 0)
		this->mergeStrategy = STRATEGY_SQRT_MERGE;
	else if (strcasecmp(mergeStrategy, "INPLACE") == 0)
		this->mergeStrategy = STRATEGY_INPLACE;
	else {
		snprintf(errorMessage, sizeof(errorMessage),
				"Illegal value for UPDATE_STRATEGY: %s", mergeStrategy);
		log(LOG_ERROR, LOG_ID, errorMessage);
		log(LOG_ERROR, LOG_ID, "Defaulting to IMMEDIATE_MERGE.");
		this->mergeStrategy = STRATEGY_IMMEDIATE_MERGE;
	}

	// setup update index and long-list index for hybrid index maintenance
	currentLongListIndex = NULL;
	newLongListIndex = NULL;
	if (this->mergeStrategy & STRATEGY_INPLACE) {
		currentLongListIndex = InPlaceIndex::getIndex(index, index->directory);
	}
	else {
		char value[MAX_CONFIG_VALUE_LENGTH + 1];
		if (getConfigurationValue("HYBRID_INDEX_MAINTENANCE", value)) {
			if (strcasecmp(value, "CONTIGUOUS") == 0) {
				updateIndex = new HybridLexicon(index, index->DOCUMENT_LEVEL_INDEXING);
				if (this->mergeStrategy != STRATEGY_IMMEDIATE_MERGE) {
					log(LOG_ERROR, LOG_ID, "Contiguous hybrid index maintenance only supported with Immediate Merge.");
					log(LOG_ERROR, LOG_ID, "Switching update strategy to HIM_c.");
					this->mergeStrategy = STRATEGY_IMMEDIATE_MERGE;
				}
				this->mergeStrategy |= STRATEGY_HYBRID;
			}
			else if (strcasecmp(value, "NON_CONTIGUOUS") == 0) {
				currentLongListIndex = InPlaceIndex::getIndex(index, index->directory);
				this->mergeStrategy |= STRATEGY_HYBRID;
			}
			else if (strcasecmp(value, "NON_CONTIGUOUS_APPEND") == 0) {
				currentLongListIndex = InPlaceIndex::getIndex(index, index->directory);
				this->mergeStrategy |= STRATEGY_HYBRID;
			}
		}
	}
	getConfigurationInt("LONG_LIST_THRESHOLD", &inPlaceLimit, 2000000);

	// initialize in-memory index for update postings
	if (updateIndex == NULL) {
		if (strcasecmp(lexiconType, "COMPRESSED_LEXICON") == 0)
			updateIndex = new CompressedLexicon(index, index->DOCUMENT_LEVEL_INDEXING);
		else if (strcasecmp(lexiconType, "REALLOC_LEXICON") == 0)
			updateIndex = new ReallocLexicon(index, index->DOCUMENT_LEVEL_INDEXING);
		else if (strcasecmp(lexiconType, "TERABYTE_LEXICON") == 0)
			updateIndex = new TerabyteLexicon(index, index->DOCUMENT_LEVEL_INDEXING);
		else {
			snprintf(errorMessage, sizeof(errorMessage),
					"Illegal value for LEXICON_TYPE: %s", lexiconType);
			log(LOG_ERROR, LOG_ID, errorMessage);
			log(LOG_ERROR, LOG_ID, "Defaulting to COMPRESSED_LEXICON.");
			updateIndex = new CompressedLexicon(index, index->DOCUMENT_LEVEL_INDEXING);
		}
	}

#if SUPPORT_APPEND_TAIT
	// append with posting transformation at indexing time is only supported by
	// the CompressedLexicon implementation
	char className[256];
	updateIndex->getClassName(className);
	assert(strcmp(className, "CompressedLexicon") == 0);
#endif

	// load on-disk indices and immediately save (in order to create .list file)
	loadOnDiskIndices();
	saveOnDiskIndices();
	newIndexTimeStamp = -1;

	// initialize non-essential member variables
	lastPartialFlushWasPointless = false;
} // end of OnDiskIndexManager()


void OnDiskIndexManager::runBuildTask() {
	bool mustReleaseLock = getLock();

	// update garbage information for in-memory update index
	offset fp, lp;
	updateIndex->getIndexRange(&fp, &lp);
	(*indexList)["index.mem"].firstPosting = fp;
	(*indexList)["index.mem"].lastPosting = lp;

	bool buildPhysically =
		((mergeStrategy & (STRATEGY_NO_MERGE | STRATEGY_INPLACE)) || (shutdownInitiated) ||
		 (asyncIndexMaintenance) || (currentIndexCount == 0) || (newIndexCount > 0));
	if (mustReleaseLock)
		releaseLock();

	if (buildPhysically) {
		// in case of asynchronous index maintenance, we first build a new on-disk
		// index from the in-memory data (fast) and then do the merge business
		// later in an asynchronous fashion
		buildNewIndex();
		deregisterUser(-1);

		// It is possible that building a new index resulted in a violation of
		// the relative on-disk index size constraints: merge if necessary.
		if (asyncIndexMaintenance) {
			if ((!maintenanceTaskIsRunning) || (shutdownInitiated))
				runMaintenanceTaskAsynchronously(MAINTENANCE_TASK_MERGE);
		}
		else
			runMaintenanceTaskSynchronously(MAINTENANCE_TASK_MERGE);
	}
	else {
		// in case of synchronous index maintenance, we do not explicitly
		// build the new on-disk index, but simply integreate it in to a
		// merge operation
		runMaintenanceTaskSynchronously(MAINTENANCE_TASK_MERGE);
	}
} // end of runBuildTask()


OnDiskIndexManager::~OnDiskIndexManager() {
	bool mustReleaseLock = getLock();

	// indicate start of shutdown sequence and wait for processes to finish
	shutdownInitiated = true;
	destructorCalled = true;
	log(LOG_DEBUG, LOG_ID, "Shutting down: Waiting for processes to finish.");
	while ((userCount > 0) || (maintenanceTaskIsRunning) || (maintenanceTaskWaitCnt > 0)) {
		releaseLock();
		waitMilliSeconds(50);
		getLock();
	}
	if (mustReleaseLock)
		releaseLock();
	log(LOG_DEBUG, LOG_ID, "All processes finished. Finalizing.");
	assert(newIndexCount == 0);
	assert(newLongListIndex == NULL);
	asyncIndexMaintenance = false;

	// write current in-memory index to disk
	if (updateIndex != NULL)
		if (updateIndex->getTermCount() > 0)
			runBuildTask();

	shutdownInitiated = false;

	// perform final build and merge operations
	if (mergeStrategy == STRATEGY_INPLACE) {
		runMaintenanceTaskSynchronously(MAINTENANCE_TASK_BUILD_INDEX);
	}
	else {
		if (mergeAtExit)
			mergeStrategy = STRATEGY_IMMEDIATE_MERGE;
		else
			mergeStrategy = STRATEGY_SMALL_MERGE;
		runMaintenanceTaskSynchronously(MAINTENANCE_TASK_MERGE);
	}
	
	if (updateIndex != NULL) {
		delete updateIndex;
		updateIndex = NULL;
	}

	// save updated sub-index info to disk and free all resources
	saveOnDiskIndices();
	delete indexList;
	for (int i = 0; i < currentIndexCount; i++) {
		delete currentIndices[i];
		currentIndices[i] = NULL;
	}
	free(currentIndices);
	if (currentLongListIndex != NULL) {
		delete currentLongListIndex;
		currentLongListIndex = NULL;
	}
	assert(newIndices == NULL);
	assert(newLongListIndex == NULL);

	// finalize user and maintenance task management
	sem_destroy(&userSemaphore);
	sem_destroy(&maintenanceTaskSemaphore);	
} // end of ~OnDiskIndexManager()


int64_t OnDiskIndexManager::registerUser(int64_t suggestedID) {
	if (this == NULL)
		return -1;

	bool mustReleaseLock = getLock();
	int64_t result = -1;
	if (currentTimeStamp < suggestedID)
		currentTimeStamp = suggestedID;
	if (!shutdownInitiated) {
		result = currentTimeStamp;
		currentTimeStamp++;
	}

	// acquire "registered users" semaphore
	releaseLock();
	sem_wait(&userSemaphore);
	getLock();

	if (shutdownInitiated) {
		result = -1;
		sem_post(&userSemaphore);
	}
	else
		userList[userCount++] = result;

	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of registerUser()


void OnDiskIndexManager::deregisterUser(int64_t userID) {
	bool mustReleaseLock = getLock();
	sem_post(&userSemaphore);

	if (userID >= 0) {
		// remove user from list of active users
		bool found = false;
		for (int i = 0; i < userCount; i++)
			if (userID == userList[i]) {
				found = true;
				for (int k = i; k < userCount - 1; k++)
					userList[k] = userList[k + 1];
				userCount--;
				break;
			}
		if (!found)
			log(LOG_ERROR, LOG_ID, "User not found in deregisterUser(int64_t).");
		assert(found);
	}

	if (userCount == 0)
		activateNewIndices();
	else if ((newIndexCount > 0) && (userList[userCount - 1] > newIndexTimeStamp))
		activateNewIndices();

	if (mustReleaseLock)
		releaseLock();
} // end of deregisterUser(int64_t)


void OnDiskIndexManager::loadOnDiskIndices() {
	LocalLock lock(this);

	postingCount = deletedPostingCount = 0;
	memset(currentIndexMap, 0, sizeof(currentIndexMap));
	memset(newIndexMap, 0, sizeof(newIndexMap));
	indexList->clear();

	char *fileName = evaluateRelativePathName(index->directory, "index.list");
	FILE *f = fopen(fileName, "r");
	free(fileName);
	if (f == NULL) {
		currentIndices = typed_malloc(CompactIndex*, 1);
		currentIndexCount = 0;
	}
	else {
		char line[1024];
		int previousID = -1;
		currentIndices = typed_malloc(CompactIndex*, MAX_INDEX_COUNT + 1);
		currentIndexCount = 0;
		while (fgets(line, sizeof(line), f) != NULL) {
			if ((line[0] == '#') || (line[0] == '\n'))
				continue;
			int len = strlen(line);
			if (len < 3)
				continue;
			if (line[len - 1] == '\n')
				line[--len] = 0;

			char fName[1024];
			long long fp, lp, pCnt, dpCnt;
			int status = sscanf(line, "%s%lld%lld%lld%lld", fName, &fp, &lp, &pCnt, &dpCnt);
			if (status != 5) {
				snprintf(errorMessage, sizeof(errorMessage),
						"Illegal input line in index.list: %s", line);
				log(LOG_ERROR, LOG_ID, errorMessage);
				log(LOG_ERROR, LOG_ID, "Terminating immediately.");
				exit(1);
			}

			// initialize garbage collection information for this index
			GarbageInformation gi;
			gi.firstPosting = fp;
			gi.lastPosting = lp;
			gi.postingCount = pCnt;
			gi.deletedPostingCount = dpCnt;
			(*indexList)[fName] = gi;

			// update global statistics
			postingCount += gi.postingCount;
			deletedPostingCount += gi.deletedPostingCount;

			// create index object
			char *fileName =
				evaluateRelativePathName(index->directory, extractLastComponent(fName, false));
			currentIndices[currentIndexCount] = CompactIndex::getIndex(index, fileName, false);

			// update free-index-id map
			int id = atoi(&fName[strlen(fName) - 3]);
			currentIndexMap[id] = 1;
			if (id <= previousID)
				log(LOG_ERROR, LOG_ID, "Sub-indices in non-ascending order!");
			assert(id > previousID);
			previousID = id;
			free(fileName);
			currentIndexCount++;
		}
		fclose(f);
		currentIndices = typed_realloc(CompactIndex*, currentIndices, currentIndexCount + 1);
	}

	// initialize set of new indices to non-active
	newIndexCount = 0;
	newIndices = NULL;
	memset(newIndexMap, 0, sizeof(newIndexMap));
	newIndexTimeStamp = -1;
} // end of loadOnDiskIndices()


void OnDiskIndexManager::saveOnDiskIndices() {
	LocalLock lock(this);
	if (this->index->readOnly)
		return;

	char *fileName = evaluateRelativePathName(index->directory, "index.list");
	FILE *f = fopen(fileName, "w");
	if (f == NULL) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to create file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		free(fileName);
		return;
	}
	else
		free(fileName);
	fprintf(f, "# This file contains a list of all on-disk indices that belong to the\n");
	fprintf(f, "# index directory that this file is located in.\n\n");
	for (int i = 0; i < currentIndexCount; i++) {
		char *fileName = currentIndices[i]->getFileName();
		char *fn = extractLastComponent(fileName, false);
		std::map<std::string, GarbageInformation>::iterator iter = indexList->find(fn);
		assert(iter != indexList->end());
		GarbageInformation gi = iter->second;
		fprintf(f, "%s %lld %lld %lld %lld\n",
				fn, gi.firstPosting, gi.lastPosting, gi.postingCount, gi.deletedPostingCount);
		free(fileName);
	}
	fclose(f);
} // end of saveOnDiskIndices()


void OnDiskIndexManager::notifyOfAddressSpaceChange(int signum, offset start, offset end) {
	LocalLock lock(this);
	if (end < start)
		return;

	// propagate garbage information to in-memory index if ranges intersect
	offset fp, lp;
	updateIndex->getIndexRange(&fp, &lp);
	if (end >= fp) {
		offset intersection = MIN(end - start, end - fp) + 1;
		if (intersection > 0) {
			if (signum > 0)
				(*indexList)["index.mem"].postingCount += intersection;
			else
				(*indexList)["index.mem"].deletedPostingCount += intersection;
		}
	}

	// for each of the existing on-disk indices, check whether the index range
	// intersects with range given by (start, end); if so, update garbage
	// information for index partition affected by change
	std::map<std::string, GarbageInformation>::iterator iter;
	for (iter = indexList->begin(); iter != indexList->end(); ++iter) {
		if (iter->first == "index.mem")
			continue;
		offset s = MAX(start, iter->second.firstPosting);
		offset e = MIN(end, iter->second.lastPosting);
		if (s <= e) {
			offset intersection = (e - s) + 1;
			if (signum > 0)
				iter->second.postingCount += intersection;
			else if (signum < 0)
				iter->second.deletedPostingCount += intersection;
		}
	}

	// update global statistics and start garbage collector if necessary
	if (signum > 0)
		postingCount += end - start + 1;
	else if (signum < 0)
		deletedPostingCount += end - start + 1;
	if (deletedPostingCount > postingCount * garbageThreshold)
		if (deletedPostingCount > 16384)
			triggerGarbageCollection();
} // end of notifyOfAddressSpaceChange(int, offset, offset)


void OnDiskIndexManager::clearUpdateIndex() {
	LocalLock lock(this);
	updateIndex->clear();
	(*indexList)["index.mem"].postingCount = 0;
	(*indexList)["index.mem"].deletedPostingCount = 0;
} // end of clearUpdateIndex()


void OnDiskIndexManager::deleteOldIndexFiles_SYNC(void *data) {
	ScheduledForDeletion *sfd = (ScheduledForDeletion*)data;
	for (int i = 0; i < sfd->toDeleteCount; i++) {
		char *fileName = sfd->toDelete[i];
		unlink(fileName);
		bool mustReleaseLock = getLock();
		int id, len = strlen(fileName);
		if (sscanf(&fileName[len - 3], "%d", &id) == 1)
			currentIndexMap[id] = 0;
		indexList->erase(extractLastComponent(fileName, false));
		if (mustReleaseLock)
			releaseLock();
		free(fileName);
	}
	free(sfd);
	bool mustReleaseLock = getLock();
	maintenanceTaskWaitCnt--;
	if (mustReleaseLock)
		releaseLock();
} // end of deleteOldIndexFiles_SYNC(ScheduledForDeletion*)


static void * deleteOldIndexFiles_ASYNC(void *data) {
	ScheduledForDeletion *sfd = (ScheduledForDeletion*)data;
	sfd->indexManager->deleteOldIndexFiles_SYNC(sfd);
	return NULL;
} // end of deleteOldIndexFiles_ASYNC(void*)


void OnDiskIndexManager::activateNewIndices() {
	if (newIndexCount <= 0)
		return;

	bool mustReleaseLock = getLock();

	ScheduledForDeletion *sfd = typed_malloc(ScheduledForDeletion, 1);
	sfd->indexManager = this;
	sfd->toDeleteCount = 0;

	if (newIndexCount > 0) {
		// free all resources occupied by the old index set
		for (int i = 0; i < currentIndexCount; i++)
			delete currentIndices[i];
		free(currentIndices);

		// for every file that belongs to the old index set, but not to the new
		// one, delete that file
		for (int i = 0; i < MAX_INDEX_COUNT; i++) {
			if ((currentIndexMap[i]) && (!newIndexMap[i]))
				sfd->toDelete[sfd->toDeleteCount++] = createFileName(i);
			if (newIndexMap[i])
				currentIndexMap[i] = 1;
		}

		// copy new index set to "currentIndices" array etc.
		currentIndices = newIndices;
		currentIndexCount = newIndexCount;
		memcpy(currentIndexMap, newIndexMap, sizeof(currentIndexMap));
		newIndexCount = 0;
		newIndices = NULL;
		memset(newIndexMap, 0, sizeof(newIndexMap));

		// update on-disk meta-data
		saveOnDiskIndices();
		index->invalidateCacheContent();
	} // end if (newIndexCount > 0)

	if (newLongListIndex != NULL) {
		if (currentLongListIndex != NULL) {
			sfd->toDelete[sfd->toDeleteCount++] = currentLongListIndex->getFileName();
			delete currentLongListIndex;
		}
		currentLongListIndex = newLongListIndex;
		newLongListIndex = NULL;
	} // end if (newLongListIndex != NULL)

	newIndexTimeStamp = -1;

	if (sfd->toDeleteCount > 0) {
		maintenanceTaskWaitCnt++;
		if (asyncIndexMaintenance) {
			pthread_t thread;
			pthread_create(&thread, NULL, deleteOldIndexFiles_ASYNC, sfd);
			pthread_detach(thread);
		}
		else
			deleteOldIndexFiles_SYNC(sfd);
	}
	else
		free(sfd);

	if (mustReleaseLock)
		releaseLock();
} // end of activateNewIndices()


void OnDiskIndexManager::addPostings(char **terms, offset *postings, int count) {
	if ((this == NULL) || (count <= 0))
		return;
	LocalLock lock(this);
	updateIndex->addPostings(terms, postings, count);
	updateIndex->extendIndexRange(postings[0], postings[count - 1]);
	if (updateIndex->memoryOccupied > updateMemoryLimit)
		runBuildTask();
} // end of addPostings(char**, offset*, int)


void OnDiskIndexManager::addPostings(char *term, offset *postings, int count) {
	if (this == NULL)
		return;
	LocalLock lock(this);
	updateIndex->addPostings(term, postings, count);
	updateIndex->extendIndexRange(postings[0], postings[count - 1]);
	if (updateIndex->memoryOccupied > updateMemoryLimit)
		runBuildTask();
} // end of addPostings(char*, offset*, int)


void OnDiskIndexManager::addPostings(InputToken *terms, int count) {
	if (this == NULL)
		return;
	LocalLock lock(this);
	updateIndex->addPostings(terms, count);
	updateIndex->extendIndexRange(terms[0].posting, terms[count - 1].posting);
	if (updateIndex->memoryOccupied > updateMemoryLimit)
		runBuildTask();
} // end of addPostings(InputToken*, int)


static void updateGarbageInformation(GarbageInformation *gi, GarbageInformation deltaGI) {
	gi->firstPosting = MIN(gi->firstPosting, deltaGI.firstPosting);
	gi->lastPosting = MAX(gi->lastPosting, deltaGI.lastPosting);
	gi->postingCount += deltaGI.postingCount;
	gi->deletedPostingCount += deltaGI.deletedPostingCount;	
} // end of updateGarbageInformation(...)


/**
 * Builds a new on-disk inverted file from data found in the in-memory index.
 * This method is usually executed synchronously.
 **/
void OnDiskIndexManager::buildNewIndex() {
	if (updateIndex->termCount == 0)
		return;
	if (currentIndexCount >= MAX_INDEX_COUNT) {
		log(LOG_ERROR, LOG_ID, "Maximum index count reached. Refusing to build new index.");
		return;
	}

	// if we are using the in-place index, simply copy in-memory postings to disk
	if (mergeStrategy & STRATEGY_INPLACE) {
		sprintf(errorMessage, "Commencing in-place update: %lld terms with %lld postings in index.",
				static_cast<long long>(currentLongListIndex->getTermCount()),
				static_cast<long long>(currentLongListIndex->getPostingCount()));
		log(LOG_DEBUG, LOG_ID, errorMessage);

		bool partialFlush;
		getConfigurationBool("PARTIAL_FLUSH", &partialFlush, false);
		int threshold = 1;
		if (partialFlush)
			getConfigurationInt("LONG_LIST_THRESHOLD", &threshold, 1);

		if ((threshold <= 1) || (shutdownInitiated) || (lastPartialFlushWasPointless)) {
			// if the threshold is smaller than 1, partial flush has not been requested;
			// proceed as usual
			IndexIterator *iterator = updateIndex->getIterator();
			IndexMerger::mergeIndices(index, currentLongListIndex, iterator, NULL, asyncIndexMaintenance);
			delete iterator;
			clearUpdateIndex();
			index->invalidateCacheContent();
			lastPartialFlushWasPointless = false;
		}
		else {
			// otherwise, write all lists >= threshold out to disk and keep the rest
			// in memory
			int oldMemoryConsumption = updateIndex->memoryOccupied;

			IndexIterator *iterator =
				new ThresholdIterator(updateIndex->getIterator(), threshold, INT_MAX);
			IndexMerger::mergeIndices(index, currentLongListIndex, iterator, NULL, asyncIndexMaintenance);
			delete iterator;
			updateIndex->clear(threshold);

			// check whether we are saving sufficiently much memory by deferring
			// updating short on-disk lists; if not, make sure that we run a complete
			// flush the next time
			if (updateIndex->memoryOccupied > oldMemoryConsumption * 0.85)
				lastPartialFlushWasPointless = true;

			index->invalidateCacheContent();
		}

		sprintf(errorMessage, "In-place update finished: %d terms with %lld postings in index.",
				static_cast<int>(currentLongListIndex->getTermCount()),
				static_cast<long long>(currentLongListIndex->getPostingCount()));
		log(LOG_DEBUG, LOG_ID, errorMessage);
		return;
	} // end if (mergeStrategy & STRATEGY_INPLACE)

	bool mustReleaseLock = getLock();

	// determine whether the new index goes into the "currentIndices" or into
	// the "newIndices" array
	bool isNewIndex = (newIndexCount > 0);

	// create inverted file from in-memory index
	int id = findHighestUsedID() + 1;
	char *newFileName = createFileName(id);
	sprintf(errorMessage, "Adding index to %s index set: %s", (isNewIndex ? "new" : "current"), newFileName);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	if (mergeStrategy & STRATEGY_HYBRID) {
		bool includeMap[MAX_INDEX_COUNT];
		for (int i = 0; i < currentIndexCount; i++)
			includeMap[0] = false;
		int newFlag = 0;
		if (currentLongListIndex != NULL)
			updateBitMasks(includeMap, currentLongListIndex, &newFlag);
		IndexIterator *iterator = updateIndex->getIterator();
		CompactIndex *targetIndex = CompactIndex::getIndex(index, newFileName, true);
		if ((isNewIndex) || (currentIndexCount > 0))
			doMerge(iterator, targetIndex, NULL, false, false, newFlag);
		else
			doMerge(iterator, targetIndex, currentLongListIndex, false, true, newFlag);
		delete iterator;
		delete targetIndex;
	}
	else
		updateIndex->createCompactIndex(newFileName);

	// propagate index coverage information from in-memory to on-disk index
	offset fp, lp;
	updateIndex->getIndexRange(&fp, &lp);
	GarbageInformation gi = (*indexList)["index.mem"];
	gi.firstPosting = fp;
	gi.lastPosting = lp;
	gi.deletedPostingCount = MIN(gi.deletedPostingCount, gi.postingCount);
	(*indexList)[extractLastComponent(newFileName, false)] = gi;
	clearUpdateIndex();

	if (isNewIndex) {
		newIndexCount++;
		newIndices = typed_realloc(CompactIndex*, newIndices, newIndexCount);
		newIndices[newIndexCount - 1] = CompactIndex::getIndex(index, newFileName, false);
		assert(newIndexMap[id] == 0);
		newIndexMap[id] = 1;
	}
	else {
		currentIndexCount++;
		currentIndices = typed_realloc(CompactIndex*, currentIndices, currentIndexCount);
		currentIndices[currentIndexCount - 1] = CompactIndex::getIndex(index, newFileName, false);
		assert(currentIndexMap[id] == 0);
		currentIndexMap[id] = 1;
	}
	free(newFileName);
	saveOnDiskIndices();
	index->invalidateCacheContent();

	if (mustReleaseLock)
		releaseLock();
} // end of buildNewIndex()


void OnDiskIndexManager::sync() {
	LocalLock lock(this);
	buildNewIndex();
	if (asyncIndexMaintenance)
		runMaintenanceTaskAsynchronously(MAINTENANCE_TASK_MERGE);
	else
		runMaintenanceTaskSynchronously(MAINTENANCE_TASK_MERGE);
} // end of sync()


static void printIndexInfo(OnDiskIndex *index) {
#if 0
	if (index == NULL)
		return;
	char line[1024];
	char *fileName = index->getFileName();
	snprintf(line, sizeof(line), "After merge: %s contains %d terms with " OFFSET_FORMAT " postings.",
			fileName, index->getTermCount(), index->getPostingCount());
	free(fileName);
#endif
} // end of printIndexInfo(OnDiskIndex*)


void OnDiskIndexManager::doMerge(IndexIterator *iterator, OnDiskIndex *target,
		InPlaceIndex *longListTarget, bool withGC, bool mayAddNewTermsToLong, int newFlag) {
	if (destructorCalled) {
		// System is shutting down. Notify the user that this might take a while.
		fprintf(stderr, "Merging index files. Please wait.\n");
	}

	if (withGC) {
		log(LOG_DEBUG, LOG_ID, "Merging indices with built-in garbage collection.");
		VisibleExtents *visible = index->getVisibleExtents(Index::SUPERUSER, true);
		ExtentList *list = visible->getExtentList();
		IndexMerger::mergeIndices(index, target, iterator, list, asyncIndexMaintenance);
		delete list;
		delete visible;
	}
	else {
		log(LOG_DEBUG, LOG_ID, "Merging indices without built-in garbage collection.");
		if (longListTarget == NULL)
			IndexMerger::mergeIndices(
					index, target, iterator, NULL, asyncIndexMaintenance);
		else
			IndexMerger::mergeWithLongTarget(index, target, iterator, longListTarget,
					inPlaceLimit, mayAddNewTermsToLong, newFlag);
	}
	printIndexInfo(target);
	printIndexInfo(longListTarget);
} // end of doMerge(IndexIterator*, OnDiskIndex*, bool)


/** Runs the garbage collector. **/
void OnDiskIndexManager::runGC() {
	assert(maintenanceTaskIsRunning);

	// we can only perform the garbage collection if there is currently no set
	// of new indices and if there is a non-empty set of old indices
	if ((newIndexCount > 0) || (currentIndexCount == 0))
		return;
	if ((currentLongListIndex != NULL) || (mergeStrategy & (STRATEGY_HYBRID | STRATEGY_INPLACE))) {
		log(LOG_ERROR, LOG_ID, "The current version of the garbage collector is incompatible with");
		log(LOG_ERROR, LOG_ID, "in-place or hybrid index maintenance. Sorry!");
		exit(1);
	}

	bool mustReleaseLock = getLock();

	int id = findFirstFreeID(0);
	newIndexMap[id] = 1;
	char *fileName = createFileName(id);
	CompactIndex *targetIndex = CompactIndex::getIndex(index, fileName, true, asyncIndexMaintenance);

	InPlaceIndex *longListIndex = NULL;
	if (mergeStrategy & STRATEGY_HYBRID)
		longListIndex = currentLongListIndex;

	// create iterators for all input indices
	IndexIterator *iterator;
	IndexIterator **iterators = typed_malloc(IndexIterator*, currentIndexCount + 2);
	int cnt = 0;
	int bufferSize = TOTAL_MERGE_BUFFER_SIZE /
		(currentIndexCount + (currentLongListIndex == NULL ? 0 : 1));

	// create input iterators and propagate garbage information to new index
	GarbageInformation gi;
	gi.firstPosting = MAX_OFFSET;
	gi.lastPosting = 0;
	gi.postingCount = 0;
	gi.deletedPostingCount = 0;

	int currentIndexCountBeforeMerge = currentIndexCount;

	for (int i = 0; i < currentIndexCount; i++) {
		char *fileName = currentIndices[i]->getFileName();
		iterators[cnt++] = CompactIndex::getIterator(fileName, bufferSize);
		updateGarbageInformation(&gi, (*indexList)[extractLastComponent(fileName, false)]);
		free(fileName);
	}

	// copy garbage statistics to management struct for target index
	postingCount = gi.postingCount = MAX(0, gi.postingCount - gi.deletedPostingCount);
	deletedPostingCount = gi.deletedPostingCount = 0;
	(*indexList)[extractLastComponent(fileName, false)] = gi;

	if (mustReleaseLock)
		releaseLock();

	if (currentLongListIndex != NULL)
		iterators[cnt++] = currentLongListIndex->getIterator(bufferSize);
	if (cnt == 1) {
		iterator = iterators[0];
		free(iterators);
	}
	else
		iterator = new MultipleIndexIterator(iterators, cnt);

	// perform the actual merge and free resources when we are done
	doMerge(iterator, targetIndex, longListIndex, true);
	delete iterator;

	if (targetIndex != NULL) {
		char *fileName = targetIndex->getFileName();
		delete targetIndex;
		targetIndex = CompactIndex::getIndex(index, fileName, false);
		free(fileName);
	}
	if (longListIndex != NULL) {
		delete longListIndex;
		longListIndex = InPlaceIndex::getIndex(index, index->directory);
	}

	// update meta-information
	mustReleaseLock = getLock();
	newIndices = typed_malloc(CompactIndex*, currentIndexCount + 1);
	newIndices[0] = targetIndex;
	newIndexCount = 1;
	newIndexMap[id] = 1;

	// add all indices that have been created AFTER this maintenance task
	// was started (annoying situation, but we have to deal with it)
	for (int i = currentIndexCountBeforeMerge; i < currentIndexCount; i++) {
		char *fileName = currentIndices[i]->getFileName();
		int thisID = atoi(&fileName[strlen(fileName) - 3]);
		assert(thisID > id);
		newIndices[newIndexCount++] = CompactIndex::getIndex(index, fileName, false);
		newIndexMap[thisID] = 1;
		free(fileName);
	}

	if (mustReleaseLock)
		releaseLock();
	free(fileName);
} // end of runGC()


IndexIterator * OnDiskIndexManager::createIterator(bool *includeMap, bool includeUpdateIndex) {
	int inputIndexCount = 0;
	for (int i = 0; i < currentIndexCount; i++)
		if (includeMap[i])
			inputIndexCount++;
	if (inputIndexCount == 0) {
		if (includeUpdateIndex)
			return updateIndex->getIterator();
		else
			return NULL;
	}
	IndexIterator **iterators = typed_malloc(IndexIterator*, currentIndexCount + 2);
	int bufferSize = TOTAL_MERGE_BUFFER_SIZE / inputIndexCount;
	int iteratorCount = 0;
	for (int i = 0; i < currentIndexCount; i++) {
		if (includeMap[i]) {
			char *fileName = currentIndices[i]->getFileName();
			iterators[iteratorCount++] = CompactIndex::getIterator(fileName, bufferSize);
			free(fileName);
		}
	}
	if (includeUpdateIndex)
		iterators[iteratorCount++] = updateIndex->getIterator();
	return new MultipleIndexIterator(iterators, iteratorCount);
} // end of createIterator(bool*, bool)


void OnDiskIndexManager::updateBitMasks(bool *includeMap, InPlaceIndex *longListIndex, int *newFlag) {
	LocalLock lock(longListIndex);
	int included = 0;
	for (int i = 0; i < currentIndexCount; i++)
		if (includeMap[i])
			included++;
	int notIncluded = currentIndexCount - included;
	int andMask = (1 << notIncluded) - 1;
	int orMask = (1 << notIncluded);

	std::map<std::string,InPlaceTermDescriptor>::iterator iter;
	for (iter = longListIndex->termMap->begin(); iter != longListIndex->termMap->end(); ++iter) {
		InPlaceTermDescriptor *descriptor = &iter->second;
		descriptor->appearsInIndex &= andMask;
		descriptor->appearsInIndex |= orMask;
	}
	*newFlag = orMask;
} // end of updateBitMasks(bool, InPlaceIndex*, int*)


void OnDiskIndexManager::checkVMT() {
}


void OnDiskIndexManager::computeIndexSetForMergeOperation(
		int mergeStrategy, bool *includeInMerge, bool *includeUpdateIndex, int *indicesInvolved) {
	for (int i = 0; i < MAX_INDEX_COUNT; i++)
		includeInMerge[i] = false;
	*includeUpdateIndex = false;
	*indicesInvolved = 0;

	// if we are updating the on-disk index synchronously, we might want to
	// include the current in-memory index; check whether it makes sense
	// (memory consumption bigger than 40% of threshold) and include in
	// update operation as appropriate
	if (!asyncIndexMaintenance) {
		if (updateIndex->memoryOccupied > updateMemoryLimit * 0.4) {
			*includeUpdateIndex = true;
			*indicesInvolved = *indicesInvolved + 1;
		}
	}

	switch (mergeStrategy % STRATEGY_HYBRID) {
		case STRATEGY_INPLACE:
		case STRATEGY_NO_MERGE:
			// don't do anything
			break;
		case STRATEGY_IMMEDIATE_MERGE:
			// include all index partitions in merge operation
			for (int i = 0; i < currentIndexCount; i++) {
				includeInMerge[i] = true;
				*indicesInvolved = *indicesInvolved + 1;
			}
			break;
		case STRATEGY_SQRT_MERGE:
			{
				if (currentIndexCount <= 1) {
					// new data go into second-ondisk index => do nothing yet
				}
				else if (currentIndexCount == 2) {
					// check whether small index is bigger than sqrt of big index; if so,
					// merge them; otherwise, just merge in-memory index with small on-disk index
					double relSize1 =
						MAX(0.5, currentIndices[0]->getByteSize() * 1.0 / updateMemoryLimit);
					double relSize2 =
						MAX(0.5, currentIndices[1]->getByteSize() * 1.0 / updateMemoryLimit);
					if (includeUpdateIndex)
						relSize2 += updateIndex->memoryOccupied * 1.0 / updateMemoryLimit;
					includeInMerge[1] = true;
					*indicesInvolved = *indicesInvolved + 1;
					if (relSize2 > sqrt(relSize1)) {
						includeInMerge[0] = true;
						*indicesInvolved = *indicesInvolved + 1;
					}
				}
				else {
					// if we have more than 2 on-disk indices, something must have gone wrong
					// (or the user restarted the engine with a different update strategy);
					// to be on the safe side, perform global re-merge
					for (int i = 0; i < currentIndexCount; i++) {
						includeInMerge[i] = true;
						*indicesInvolved = *indicesInvolved + 1;
					}
				}
			}
			break;
		case STRATEGY_LOG_MERGE:
			{
				// Collect all indices that violate the logmerge index size requirements
				// (exponentially growing for base 2).
				int64_t indexSizeSoFar = 0;
				if (mergeStrategy & STRATEGY_HYBRID) {
					// this is the annoying case; in hybrid index maintenance, we can run into
					// the situation where the sum of a bunch of indices is greater than the size
					// of another index, but that other index still must not be included in the
					// merge operation, because its nominal size (address space covered) is greater
					// than that of all the other indices...
					if (*includeUpdateIndex)
						indexSizeSoFar += (*indexList)["index.mem"].postingCount;
					for (int i = currentIndexCount - 1; i >= 0; i--) {
						char *fName = currentIndices[i]->getFileName();
						int64_t indexSize = (*indexList)[extractLastComponent(fName, false)].postingCount;
						free(fName);
						if (indexSize > indexSizeSoFar * 1.4)
							break;
						includeInMerge[i] = true;
						*indicesInvolved = *indicesInvolved + 1;
						indexSizeSoFar += indexSize;
					}
				}
				else {
					// this is the "easy" case: Logarithmic Merge, comparing the relative
					// sizes of all index partitions
					if (*includeUpdateIndex)
						indexSizeSoFar += updateIndex->memoryOccupied - updateIndex->termCount * MAX_TOKEN_LENGTH / 2;
					for (int i = currentIndexCount - 1; i >= 0; i--) {
						int64_t indexSize = currentIndices[i]->getByteSize();
						if ((*indicesInvolved > 0) && (indexSize > indexSizeSoFar * 1.4))
							break;
						includeInMerge[i] = true;
						*indicesInvolved = *indicesInvolved + 1;
						indexSizeSoFar += indexSize;
					}
				}
			}
			break;
		case STRATEGY_SMALL_MERGE:
			{
				// compute index sizes relative to maximum in-memory buffer load; merge
				// all indices that are smaller than 40% of the buffer load; SMALL_MERGE
				// is used during the shutdown operation to ensure that the number of
				// on-disk index partitions caused by partial buffer fills does not grow
				// beyond all bounds
				double relativeIndexSize[MAX_INDEX_COUNT];
				for (int i = 0; i < currentIndexCount; i++) {
					relativeIndexSize[i] =
						(1.0 * currentIndices[i]->getByteSize()) / updateMemoryLimit;
					includeInMerge[i] = (relativeIndexSize[i] < 0.4);
					if (i > 0)
						includeInMerge[i] |= includeInMerge[i - 1];
					if (includeInMerge[i])
						*indicesInvolved = *indicesInvolved + 1;
				}
			}
			break;
		default:
			assert("This should never happen!" == NULL);
	}
} // end of computeIndexSetForMergeOperation(bool*, bool*, int*)


void OnDiskIndexManager::mergeIndicesIfNecessary() {
	assert(maintenanceTaskIsRunning);

	// we can only perform the merge operation if there is currently no set
	// of new indices and if there is a non-empty set of old indices
	if ((newIndexCount > 0) || (currentIndexCount <= 0))
		return;

	int indicesInvolved = 0;
	bool includeUpdateIndex = false;
	bool includeInMerge[MAX_INDEX_COUNT];

	bool mustReleaseLock = getLock();

	// fill in the "includeInMerge" array, based on the current merge strategy
	computeIndexSetForMergeOperation(
			mergeStrategy, includeInMerge, &includeUpdateIndex, &indicesInvolved);

	// if we only have a single index to merge, return immediately
	if ((indicesInvolved <= 1) && (!includeUpdateIndex)) {
		if (mustReleaseLock)
			releaseLock();
		return;
	}

	// create iterators for all input indices
	IndexIterator *iterator = createIterator(includeInMerge, includeUpdateIndex);
	if (iterator == NULL) {
		if (mustReleaseLock)
			releaseLock();
		return;
	}

	// find a free index ID for the target index
	int lastIndexNotPartOfMerge = -1;
	for (int i = 0; i < currentIndexCount; i++)
		if (!includeInMerge[i])
			lastIndexNotPartOfMerge = i;
	int id;
	if (lastIndexNotPartOfMerge < 0)
		id = findFirstFreeID(0);
	else {
		char *fileName = currentIndices[lastIndexNotPartOfMerge]->getFileName();
		id = findFirstFreeID(atoi(&fileName[strlen(fileName) - 3]) + 1);
		free(fileName);
	}

	// collect garbage information from all indices involved in the merge operation
	GarbageInformation gi;
	gi.firstPosting = MAX_OFFSET;
	gi.lastPosting = 0;
	gi.postingCount = 0;
	gi.deletedPostingCount = 0;

	int currentIndexCountBeforeMerge = currentIndexCount;

	if (includeUpdateIndex)
		updateGarbageInformation(&gi, (*indexList)["index.mem"]);		
	for (int i = 0; i < currentIndexCount; i++) {
		if (includeInMerge[i]) {
			char *fileName = currentIndices[i]->getFileName();
			updateGarbageInformation(&gi, (*indexList)[extractLastComponent(fileName, false)]);
			free(fileName);
		}
	}

	// check whether the amount of garbage in the input indices is large enough to
	// turn on the garbage collector
	double garbageRatio = gi.deletedPostingCount * 1.0 / gi.postingCount;
	bool withGC = (garbageRatio > onTheFlyGarbageThreshold);
	sprintf(errorMessage, "Garbage ratio = %.4lf (%lld/%lld)",
			garbageRatio, gi.deletedPostingCount, gi.postingCount);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	// create target index and propagate garbage information
	char *fileName = createFileName(id);
	newIndexMap[id] = 1;
	CompactIndex *targetIndex = CompactIndex::getIndex(index, fileName, true, asyncIndexMaintenance);
	if (withGC) {
		postingCount = MAX(0, postingCount - gi.deletedPostingCount);
		deletedPostingCount = MAX(0, deletedPostingCount - gi.deletedPostingCount);
		gi.postingCount = MAX(0, gi.postingCount - gi.deletedPostingCount);
		gi.deletedPostingCount = 0;
	}
	(*indexList)[extractLastComponent(fileName, false)] = gi;

	if (mustReleaseLock)
		releaseLock();

	// include longListIndex in the merge if we use hybrid index maintenance
	int newFlag = 0;
	InPlaceIndex *longListIndex = NULL;
	if (mergeStrategy & STRATEGY_HYBRID) {
		longListIndex = currentLongListIndex;
		if (longListIndex != NULL)
			updateBitMasks(includeInMerge, longListIndex, &newFlag);
	}

	// perform the actual merge and free resources when we are done
	doMerge(iterator, targetIndex, longListIndex, withGC, includeInMerge[0], newFlag);
	delete iterator;

	mustReleaseLock = getLock();
	if (includeUpdateIndex)
		clearUpdateIndex();

	// close and re-open target index, to make sure all index data are properly
	// flushed to disk
	if (targetIndex != NULL) {
		char *fileName = targetIndex->getFileName();
		delete targetIndex;
		targetIndex = CompactIndex::getIndex(index, fileName, false);
		free(fileName);
	}

	// Update meta-information. This includes copying indices that were not
	// involved in the merge process to the new index list.
	newIndices = typed_malloc(CompactIndex*, currentIndexCount + 1);
	newIndexCount = 0;
	memset(newIndexMap, 0, sizeof(newIndexMap));

	// move all old indices that were not part of the merge operation
	// over to new index set
	for (int i = 0; i < currentIndexCountBeforeMerge; i++) {
		if (!includeInMerge[i]) {
			char *fileName = currentIndices[i]->getFileName();
			int thisID = atoi(&fileName[strlen(fileName) - 3]);
			assert(thisID < id);
			newIndices[newIndexCount++] = CompactIndex::getIndex(index, fileName, false);
			newIndexMap[thisID] = 1;
			free(fileName);
		}
	}

	// move target index of merge operation into new index set
	newIndices[newIndexCount++] = targetIndex;
	newIndexMap[id] = 1;

	// move all indices that were created AFTER this merge operation started
	// into the new index set (ANNOYING!)
	for (int i = currentIndexCountBeforeMerge; i < currentIndexCount; i++) {
		char *fileName = currentIndices[i]->getFileName();
		int thisID = atoi(&fileName[strlen(fileName) - 3]);
		assert(thisID > id);
		newIndices[newIndexCount++] = CompactIndex::getIndex(index, fileName, false);
		newIndexMap[thisID] = 1;
		free(fileName);
	}

	if (mustReleaseLock)
		releaseLock();
	free(fileName);
} // end of mergeIndicesIfNecessary()


void OnDiskIndexManager::triggerGarbageCollection() {
	LocalLock lock(this);
	// refuse to run garbage collector if the main controller has already requested shutdown
	if (shutdownInitiated)
		return;

	// check whether there is anything to do at all
	std::map<std::string, GarbageInformation>::iterator iter;
	bool nothingToDo = true;
	for (iter = indexList->begin(); iter != indexList->end(); ++iter)
		if (iter->second.deletedPostingCount > 0)
			nothingToDo = false;
	if (nothingToDo)
		return;

	if (asyncIndexMaintenance)
		runMaintenanceTaskAsynchronously(MAINTENANCE_TASK_GC);
	else
		runMaintenanceTaskSynchronously(MAINTENANCE_TASK_GC);
} // end of deleteAddressSpace(offset, offset)


ExtentList * OnDiskIndexManager::getPostings(const char *term, bool fromDisk, bool fromMemory) {
	LocalLock lock(this);

	if ((strchr(term, '$') != NULL) || (strchr(term, '*') != NULL))
		if ((index->STEMMING_LEVEL < 2) && (mergeStrategy & STRATEGY_HYBRID)) {
			log(LOG_ERROR, LOG_ID, "The current implementation of query-time stemming is incompatible with hybrid index maintenance.");
			log(LOG_ERROR, LOG_ID, "Sorry!");
			return new ExtentList_Empty();
		}
	if (this == NULL)
		return new ExtentList_Empty();

	int cnt = 0;
	ExtentList **lists =
		typed_malloc(ExtentList*, MAX(currentIndexCount, newIndexCount) + 2);
	if (fromDisk) {
		InPlaceTermDescriptor *descriptor = NULL;
		if (currentLongListIndex != NULL) {
			addNonEmptyExtentList(lists, currentLongListIndex->getPostings(term), &cnt);
			descriptor = currentLongListIndex->getDescriptor(term);
		}

		if (newIndexCount > 0) {
			for (int i = 0; i < newIndexCount; i++)
				addNonEmptyExtentList(lists, newIndices[i]->getPostings(term), &cnt);
		}
		else {
			if (descriptor == NULL) {
				for (int i = 0; i < currentIndexCount; i++)
					addNonEmptyExtentList(lists, currentIndices[i]->getPostings(term), &cnt);
			}
			else {
				for (int i = 0; i < currentIndexCount; i++) {
					if (descriptor->appearsInIndex & (1 << i)) {
						int oldCnt = cnt;
						addNonEmptyExtentList(lists, currentIndices[i]->getPostings(term), &cnt);
						assert(cnt > oldCnt);
					}
				}
			}
		} // end else [newIndexCount <= 0]
	} // end if (fromDisk)

	// combine the SegmentedPostingList instances of all on-disk indices into one
	// single object
	if (cnt > 1) {
		SegmentedPostingList *spl =
			(SegmentedPostingList*)Simplifier::combineSegmentedPostingLists(lists, cnt);
		if (spl != NULL) {
			for (int i = 0; i < cnt; i++)
				delete lists[i];
			lists[0] = spl;
			cnt = 1;
		}
	}

	// add postings from the in-memory update index
	if (fromMemory)
		addNonEmptyExtentList(lists, updateIndex->getUpdates(term), &cnt);

	if (cnt <= 0) {
		free(lists);
		return new ExtentList_Empty();
	}
	else if (cnt == 1) {
		ExtentList *list = lists[0];
		free(lists);
		return list;
	}
	else {
#if SUPPORT_APPEND_TAIT
		return new ExtentList_OR(lists, cnt);
#else
		return new ExtentList_OrderedCombination(lists, cnt);
#endif
	}
} // end of getPostings(char*, bool, bool)


void OnDiskIndexManager::getPostings(char **terms, int termCount,
		bool fromDisk, bool fromMemory, ExtentList **results) {
	LocalLock lock(this);

	// make sure the number of terms is small enough to be handled with the
	// amount of stack space available here
	static const int MAX_TERM_COUNT = 16;
	if (termCount > MAX_TERM_COUNT) {
		int k = MAX_TERM_COUNT / 2;
		getPostings(terms, k, fromDisk, fromMemory, results);
		getPostings(&terms[k], termCount - k, fromDisk, fromMemory, &results[k]);
		return;
	}

	// initialize management data for all "termCount" terms and fetch list from
	// in-place index (if existent)
	InPlaceTermDescriptor *descriptors[MAX_TERM_COUNT];
	ExtentList **lists[MAX_TERM_COUNT];
	int cnt[MAX_TERM_COUNT];
	for (int t = 0; t < termCount; t++) {
		descriptors[t] = NULL;
		if (terms[t] == NULL)
			continue;
		lists[t] = typed_malloc(ExtentList*, MAX(currentIndexCount, newIndexCount) + 2);
		cnt[t] = 0;
		if (currentLongListIndex != NULL) {
			descriptors[t] = currentLongListIndex->getDescriptor(terms[t]);
			addNonEmptyExtentList(lists[t], currentLongListIndex->getPostings(terms[t]), &cnt[t]);
		}
	}

	// compute permutation p that lists all terms in lexicographical order
	// (minimizes seek operations for the on-disk index); use BubbleSort (termCount is small)
	int p[MAX_TERM_COUNT];
	for (int t = 0; t < termCount; t++)
		p[t] = t;
	for (bool changed = true; changed; changed = false) {
		for (int j = 0; j < termCount - 1; j++) {
			if (strcmp(terms[p[j]], terms[p[j + 1]]) > 0) {
				int tmp = p[j];
				p[j] = p[j + 1];
				p[j + 1] = tmp;
				changed = true;
			}
		}
	} // end for (bool changed = true; changed; changed = false)

	if (newIndexCount > 0) {
		for (int i = 0; i < newIndexCount; i++) {
			for (int t = 0; t < termCount; t++) {
				if (terms[p[t]] == NULL)
					continue;
				addNonEmptyExtentList(
						lists[p[t]], newIndices[i]->getPostings(terms[p[t]]), &cnt[p[t]]);
			}
		}
	} // end if (newIndexCount > 0)
	else {
		for (int i = 0; i < currentIndexCount; i++) {
			for (int t = 0; t < termCount; t++) {
				if (terms[p[t]] == NULL)
					continue;
				if (descriptors[p[t]] == NULL) {
					// if we do not have a descriptor for this guy, simply visit all on-disk
					// indices to collect list fragments
					addNonEmptyExtentList(
							lists[p[t]], currentIndices[i]->getPostings(terms[p[t]]), &cnt[p[t]]);
				}
				else {
					// otherwise, prune the search by only requesting a list fragment from a
					// sub-index that might actually have some data for us (as indicated by
					// the value of the descriptor's "bitMask" field)
					if (descriptors[p[t]]->appearsInIndex & (1 << i))
						addNonEmptyExtentList(
								lists[p[t]], currentIndices[i]->getPostings(terms[p[t]]), &cnt[p[t]]);
				}
			}
		}
	} // end else [newIndexCount <= 0]

	// postprocessing: combine lists and clean things up
	for (int t = 0; t < termCount; t++) {
		if (terms[t] == NULL)
			continue;
		if (cnt[t] > 1) {
			SegmentedPostingList *spl =
				(SegmentedPostingList*)Simplifier::combineSegmentedPostingLists(lists[t], cnt[t]);
			if (spl != NULL) {
				for (int i = 0; i < cnt[t]; i++)
					delete lists[t][i];
				lists[t][0] = spl;
				cnt[t] = 1;
			}
		}
		if (fromMemory)
			addNonEmptyExtentList(lists[t], updateIndex->getUpdates(terms[t]), &cnt[t]);
		if (cnt[t] <= 0) {
			free(lists[t]);
			results[t] = new ExtentList_Empty();
		}
		else if (cnt[t] == 1) {
			ExtentList *l = lists[t][0];
			free(lists[t]);
			results[t] = l;
		}
		else {
#if SUPPORT_APPEND_TAIT
			results[t] = new ExtentList_OR(lists[t], cnt[t]);
#else
			results[t] = new ExtentList_OrderedCombination(lists[t], cnt[t]);
#endif
		}
	} // end for (int t = 0; t < termCount; t++)

} // end of getPostings(char**, int, bool, bool, ExtentList**)


void OnDiskIndexManager::getDictionarySize(offset *lower, offset *upper) {
	if (this == NULL) {
		*lower = *upper = 0;
		return;
	}
	bool mustReleaseLock = getLock();
	*lower = *upper = updateIndex->termCount;
	for (int i = 0; i < currentIndexCount; i++) {
		offset count = currentIndices[i]->getTermCount();
		if (count > *lower)
			*lower = count;
		*upper += count;
	}
	if (mustReleaseLock)
		releaseLock();
} // end of getDictionarySize(offset*, offset*)


void OnDiskIndexManager::addNonEmptyExtentList(ExtentList **lists, ExtentList *list, int *count) {
	if (list == NULL)
		return;
	list = Simplifier::simplifyList(list);
	if (list == NULL)
		return;
	if (list->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
		delete list;
		return;
	}
	lists[*count] = list;
	*count = *count + 1;
} // end of addNonEmptyExtentList(ExtentList**, ExtentList*, int*)


char * OnDiskIndexManager::createFileName(int id) {
	char *result = evaluateRelativePathName(index->directory, "index.");
	int len = strlen(result);
	result = (char*)realloc(result, len + 8);
	sprintf(&result[len], "%03d", id);
	return result;
} // end of createFileName(int)


int OnDiskIndexManager::findFirstFreeID(int fromWhere) {
	LocalLock lock(this);

	int result = -1;
	for (int i = fromWhere; i < MAX_INDEX_COUNT; i++)
		if ((!currentIndexMap[i]) && (!newIndexMap[i])) {
			result = i;
			break;
		}
	assert(result >= 0);
	return result;
} // end of findFreeID(char*)


int OnDiskIndexManager::findHighestUsedID() {
	LocalLock lock(this);

	int result = -1;
	for (int i = 0; i < MAX_INDEX_COUNT; i++)
		if ((currentIndexMap[i]) || (newIndexMap[i]))
			result = i;
	return result;
} // end of findHighestUsedID(char*)


/*********************************************************************************/
/* Methods for asynchronous index maintenance (merge, garbage collection, etc.). */
/*********************************************************************************/


struct MaintenanceTask {
	OnDiskIndexManager *indexManager;
	int taskID;
};


void OnDiskIndexManager::startMaintenanceTask() {
	bool mustReleaseLock = getLock();
	maintenanceTaskWaitCnt++;
	releaseLock();
	sem_wait(&maintenanceTaskSemaphore);
	getLock();
	maintenanceTaskIsRunning = true;
	maintenanceTaskWaitCnt--;
	if (mustReleaseLock)
		releaseLock();
} // end of startMaintenanceTask()


void OnDiskIndexManager::endMaintenanceTask() {
	bool mustReleaseLock = getLock();
	maintenanceTaskIsRunning = false;
	if (mustReleaseLock)
		releaseLock();
	sem_post(&maintenanceTaskSemaphore);
} // end of endMaintenanceTask()


void * runMaintenanceTask(void *data) {
	MaintenanceTask *task = (MaintenanceTask*)data;
	switch (task->taskID) {
		case OnDiskIndexManager::MAINTENANCE_TASK_BUILD_INDEX:
			task->indexManager->buildNewIndex();
			task->indexManager->deregisterUser(-1);
			break;
		case OnDiskIndexManager::MAINTENANCE_TASK_MERGE:
			task->indexManager->mergeIndicesIfNecessary();
			task->indexManager->deregisterUser(-1);
			break;
		case OnDiskIndexManager::MAINTENANCE_TASK_GC:
			task->indexManager->runGC();
			task->indexManager->deregisterUser(-1);
			break;
		default:
			break;
	}
	task->indexManager->endMaintenanceTask();
	free(task);
	return NULL;
} // end of runMaintenanceTask(void*)


void OnDiskIndexManager::runMaintenanceTaskSynchronously(int taskID) {
	LocalLock lock(this);
	if ((maintenanceTaskIsRunning) && (taskID != MAINTENANCE_TASK_BUILD_INDEX))
		return;
	sprintf(errorMessage, "Starting synchronous maintenance task: %d (strategy: %d)",
			taskID, mergeStrategy);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	startMaintenanceTask();
	MaintenanceTask *task = typed_malloc(MaintenanceTask, 1);
	task->indexManager = this;
	task->taskID = taskID;
	runMaintenanceTask(task);
} // end of runMaintenanceTaskSynchronously(int)


void OnDiskIndexManager::runMaintenanceTaskAsynchronously(int taskID) {
	LocalLock lock(this);
	// make sure the caller is not asking us to build a single index, as
	// it would not make any sense to do this asynchronously
	assert(taskID != MAINTENANCE_TASK_BUILD_INDEX);
	sprintf(errorMessage, "Starting asynchronous maintenance task: %d (strategy: %d)", taskID, mergeStrategy);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	startMaintenanceTask();
	MaintenanceTask *task = typed_malloc(MaintenanceTask, 1);
	task->indexManager = this;
	task->taskID = taskID;

	// set priority level of index maintenance thread to LOW
	pthread_t thread;
	pthread_attr_t t_attr;
	sched_param s_param;
	pthread_attr_init(&t_attr);
	pthread_attr_getschedparam(&t_attr, &s_param);
	s_param.sched_priority = 20;
	pthread_attr_setschedparam(&t_attr, &s_param);

	// create new thread and detach immediately; thread will clean up after itself
	pthread_create(&thread, &t_attr, runMaintenanceTask, task);
	pthread_detach(thread);
} // end of runMaintenanceTaskAsynchronously(int)


