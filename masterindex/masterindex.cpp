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
 * Implementation of the MasterIndex class.
 *
 * author: Stefan Buettcher
 * created: 2005-03-09
 * changed: 2007-12-17
 **/


#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "masterindex.h"
#include "master_docidcache.h"
#include "master_ve.h"
#include "../daemons/filesys_daemon.h"
#include "../daemons/conn_daemon.h"
#include "../indexcache/indexcache.h"
#include "../misc/all.h"
#include "../query/query.h"


MasterIndex::MasterIndex(const char *directory) {
	char msg[1024];
	snprintf(msg, sizeof(msg) - 1,
			"Starting master index. Connection file is in directory: %s\n", directory);
	log(LOG_DEBUG, LOG_ID, msg);

	// initialize internal data structures
	this->indexType = TYPE_MASTERINDEX;
	this->directory = duplicateString(directory);
	activeMountCount = 0;
	indexCount = 0;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
		mountPoints[i] = NULL;
		subIndexes[i] = NULL;
		unmountRequested[i] = -1;
	}
	startupOk = true;

	if (Index::TCP_PORT > 0) {
		connDaemon = new ConnDaemon(this, Index::TCP_PORT);
		if (!connDaemon->stopped())
			connDaemon->start();
		else
			startupOk = false;
	}
	else
		connDaemon = NULL;

	if (Index::MONITOR_FILESYSTEM) {
		fileSysDaemon = new FileSysDaemon(this);
		if (!fileSysDaemon->stopped())
			fileSysDaemon->start();
		else
			startupOk = false;
	}
	else
		fileSysDaemon = NULL;

} // end of MasterIndex()


MasterIndex::MasterIndex(int subIndexCount, char **subIndexDirs) {
	static const int SIZE = MAX_CONFIG_VALUE_LENGTH + 256;
	char msg[SIZE];
	int len = 0;
	if (subIndexCount < 0) {
		log(LOG_ERROR, LOG_ID, "subIndexCount < 0");
		subIndexCount = 0;
	}
	if (subIndexCount > MAX_MOUNT_COUNT) {
		log(LOG_ERROR, LOG_ID, "subIndexCount > MAX_MOUNT_COUNT");
		subIndexCount = MAX_MOUNT_COUNT;
	}
	len += sprintf(&msg[len], "Starting master index without authconn file. Subindices are in:\n");
	for (int i = 0; i < subIndexCount; i++) {
		if (len + strlen(subIndexDirs[i]) + 32 < SIZE)
			len += sprintf(&msg[len], "  %s\n", subIndexDirs[i]);
		else {
			len += sprintf(&msg[len], "  ...\n");
			break;
		}
	}
	log(LOG_DEBUG, LOG_ID, msg);

	// initialize internal data structures
	this->indexType = TYPE_MASTERINDEX;
	this->directory = NULL;
	activeMountCount = subIndexCount;
	indexCount = subIndexCount;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
		mountPoints[i] = NULL;
		subIndexes[i] = NULL;
		unmountRequested[i] = -1;
	}
	for (int i = 0; i < subIndexCount; i++) {
		mountPoints[i] = duplicateString("/");
		subIndexes[i] = new Index(subIndexDirs[i], true);
		unmountRequested[i] = -1;
	}
	startupOk = true;

	fileSysDaemon = NULL;
	if (Index::TCP_PORT > 0) {
		connDaemon = new ConnDaemon(this, Index::TCP_PORT);
		if (!connDaemon->stopped())
			connDaemon->start();
		else
			startupOk = false;
	}
	else
		connDaemon = NULL;

	cache = new IndexCache(this);
	invalidateCacheContent();
	documentIDs = new MasterDocIdCache(this);
} // end of MasterIndex(int, char**)


MasterIndex::~MasterIndex() {
	shutdownInitiated = true;

	if (fileSysDaemon != NULL) {
		delete fileSysDaemon;
		fileSysDaemon = NULL;
	}
	if (connDaemon != NULL) {
		delete connDaemon;
		connDaemon = NULL;
	}

	bool mustReleaseLock = getLock();
	while (registeredUserCount > 0) {
		releaseLock();
		waitMilliSeconds(20);
		getLock();
	}
	if (mustReleaseLock)
		releaseLock();

	// delete caches (if enabled)
	if (cache != NULL) {
		delete cache;
		cache = NULL;
	}
	if (documentIDs != NULL) {
		delete documentIDs;
		documentIDs = NULL;
	}

	for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
		if (mountPoints[i] != NULL) {
			free(mountPoints[i]);
			activeMountCount--;
		}
		if (subIndexes[i] != NULL) {
			delete subIndexes[i];
			indexCount--;
		}
	}
	assert(activeMountCount == 0);
	assert(indexCount == 0);

	if (directory != NULL) {
		free(directory);
		directory = NULL;
	}
} // end of ~MasterIndex()


char * MasterIndex::resolveSymbolicLinks(const char *event) {
	char *result = (char*)malloc(16384);
	int resultLen = 0;
	result[0] = 0;
	StringTokenizer *tok = new StringTokenizer(event, "\t");
	if (tok->hasNext())
		resultLen += sprintf(&result[resultLen], "%s", tok->getNext());
	while (tok->hasNext()) {
		char *token = tok->getNext();
		if (token[0] == '/') {
			// if it starts with '/', we assume it is a pathname => transform it!
			char realPathName[65536];
			if (realpath(token, realPathName) == NULL)
				resultLen += sprintf(&result[resultLen], "\t%s", token);
			else {
				if (strlen(realPathName) + strlen(event) < 16384)
					resultLen += sprintf(&result[resultLen], "\t%s", realPathName);
				else
					resultLen += sprintf(&result[resultLen], "\t%s", token);
			}
		}
		else
			resultLen += sprintf(&result[resultLen], "\t%s", token);
	}
	delete tok;
	return result;
} // end of resolveSymolicLinks(char*)


int MasterIndex::getMountPointForPath(const char *path) {
	LocalLock lock(this);
	int result = -1;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if (mountPoints[i] != NULL)
			if (startsWith(path, mountPoints[i])) {
				if (result < 0)
					result = i;
				else if (strlen(mountPoints[i]) > strlen(mountPoints[result]))
					result = i;
			}
	return result;
} // end of getMountPointForPath(char*)


bool MasterIndex::mayAccessFile(uid_t userID, const char *path) {
	LocalLock lock(this);
	bool result = false;
	int whichIndex = getMountPointForPath(path);
	if (whichIndex >= 0)
		if (subIndexes[whichIndex] != NULL)
			result = subIndexes[whichIndex]->mayAccessFile(userID, path);
	return result;
} // end of mayAccessFile(uid_t, char*)


bool MasterIndex::mayIndexThisFileSystem(const char *mountPoint) {
	// first, check if there is a .index_disallow file at the mount point
	char *disallowFile = evaluateRelativePathName(mountPoint, ".index_disallow");
	struct stat buf;
	if (stat(disallowFile, &buf) == 0) {
		free(disallowFile);
		return false;
	}
	free(disallowFile);

	// then, check whether the mount point appears in the list of indexable
	// file systems in the configuration file
	char **indexable = getConfigurationArray("INDEXABLE_FILESYSTEMS");
	if (indexable == NULL)
		return false;
	bool status = false;
	char *mp = NULL;
	if (endsWith(mountPoint, "/"))
		mp = duplicateString(mountPoint);
	else
		mp = concatenateStrings(mountPoint, "/");
	for (int i = 0; indexable[i] != NULL; i++) {
		if (startsWith(mp, indexable[i]))
			if (strlen(indexable[i]) >= strlen(mp) - 1)
				status = true;
		free(indexable[i]);
	}
	free(indexable);
	free(mp);
	return status;
} // end of mayIndexThisFileSystem(char*)


void MasterIndex::createSubIndexForMountPoint(const char *path) {
	LocalLock lock(this);
	struct stat buf;
	char *indexDir;
	Index *newIndex;

	char msg[256];
	snprintf(msg, sizeof(msg), "Creating sub-index for mount point: %s", path);
	log(LOG_DEBUG, LOG_ID, msg);

	// check if the mount point already exists in our database; if not, create it
	int which = -1;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if (mountPoints[i] != NULL)
			if (strcmp(mountPoints[i], path) == 0) {
				which = i;
				break;
			}
	if (which < 0) {
		for (int i = 0; i < MAX_MOUNT_COUNT; i++)
			if (mountPoints[i] == NULL) {
				mountPoints[i] = duplicateString(path);
				activeMountCount++;
				which = i;
				break;
			}
	}
	if (which < 0)
		goto createSubIndexForMountPoint_EXIT;

	// we already have an index for this mount point: do nothing
	if (subIndexes[which] != NULL)
		goto createSubIndexForMountPoint_EXIT;

	// check if we *really* want to create a new index for this mount point
	if (stat(path, &buf) != 0)
		goto createSubIndexForMountPoint_EXIT;
	if ((buf.st_mode & S_IWUSR) == 0)
		goto createSubIndexForMountPoint_EXIT;
	if ((startsWith(path, "/dev/")) || (startsWith(path, "/sys/")) || (startsWith(path, "/proc/")))
		goto createSubIndexForMountPoint_EXIT;
	if (!mayIndexThisFileSystem(path))
		goto createSubIndexForMountPoint_EXIT;

	// alright, let's do it; first, create index directory
	indexDir = evaluateRelativePathName(path, ".indexdir");
	if (stat(indexDir, &buf) == 0) {
		if (chmod(indexDir, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
			free(indexDir);
			goto createSubIndexForMountPoint_EXIT;
		}
	}
	else {
		if (mkdir(indexDir, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
			free(indexDir);
			goto createSubIndexForMountPoint_EXIT;
		}
		if (chmod(indexDir, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
			free(indexDir);
			goto createSubIndexForMountPoint_EXIT;
		}
	}

	snprintf(msg, sizeof(msg), "Creating new index for mount point: %s", path);
	log(LOG_OUTPUT, LOG_ID, msg);

	// create the Index instance and set its mount point appropriately
	newIndex = new Index(indexDir, true);
	newIndex->setMountPoint(path);
	free(indexDir);

	// update management information
	subIndexes[which] = newIndex;
	unmountRequested[which] = -1;
	indexCount++;

	snprintf(msg, sizeof(msg), "Active mount points: %d, active indexes: %d.",
			activeMountCount, indexCount);
	log(LOG_OUTPUT, LOG_ID, msg);

createSubIndexForMountPoint_EXIT:
	return;
} // end of createSubIndexForMountPoint(char*)


int MasterIndex::notify(const char *event) {
	if (strlen(event) >= 8192)
		return ERROR_SYNTAX_ERROR;

	// first, make sure no other process is updating the index at the same time
	bool mustReleaseLock = getLock();
	if (readOnly) {
		if (mustReleaseLock)
			releaseLock();
		return ERROR_READ_ONLY;
	}
	while (indexIsBeingUpdated) {
		if (mustReleaseLock)
			releaseLock();
		waitMilliSeconds(INDEX_WAIT_INTERVAL);
		mustReleaseLock = getLock();
	}
	indexIsBeingUpdated = true;
	if (mustReleaseLock)
		releaseLock();

	char *ev = resolveSymbolicLinks(event);
	event = ev;

	char *eventType;
	int eventHash;
	StringTokenizer *tok = new StringTokenizer(event, "\t");
	int statusCode = RESULT_SUCCESS;

	if (!tok->hasNext()) {
		statusCode = ERROR_SYNTAX_ERROR;
		goto notify_EXIT;
	}
	eventType = tok->getNext();

#define EVENT_CASE(S) if (strcmp(eventType, S) == 0)

	EVENT_CASE("MOUNT") {
		char *device = tok->getNext();
		char *path = tok->getNext();
		// try to create a new index iff:
		// - "path" is non-bogus and
		// - "device" is a local device (we do not create indexes for NFS mounts etc.)
		if (path != NULL) {
			if (strlen(path) > 0) {
				mustReleaseLock = getLock();
				if (path[strlen(path) - 1] == '/')
					path = duplicateString(path);
				else
					path = concatenateStrings(path, "/");
				int whichIndex = -1;
				for (int i = 0; i < MAX_MOUNT_COUNT; i++)
					if (mountPoints[i] != NULL)
						if (strcmp(mountPoints[i], path) == 0) {
							whichIndex = i;
							break;
						}
				if (whichIndex >= 0) {
					unmountRequested[whichIndex] = -1;
					if (subIndexes[whichIndex] == NULL)
						createSubIndexForMountPoint(path);
				}
				else
					createSubIndexForMountPoint(path);
				free(path);
				if (mustReleaseLock)
					releaseLock();
			}
		}
	} // end EVENT_CASE("MOUNT")

	EVENT_CASE("UMOUNT") {
		char *path = tok->getNext();
		if (path != NULL)
			if (strlen(path) > 0) {
				mustReleaseLock = getLock();
				if (path[strlen(path) - 1] == '/')
					path = duplicateString(path);
				else
					path = concatenateStrings(path, "/");
				for (int i = 0; i < MAX_MOUNT_COUNT; i++)
					if (mountPoints[i] != NULL)
						if (strcmp(mountPoints[i], path) == 0) {
							if (subIndexes[i] == NULL) {
								free(mountPoints[i]);
								mountPoints[i] = NULL;
								activeMountCount--;
							}
							else if (unmountRequested[i] < 0)
								unmountRequested[i] = registrationID;
						}
				free(path);
				if (mustReleaseLock)
					releaseLock();
			} // end if (strlen(path) > 0)
	} // end EVENT_CASE("UMOUNT")

	EVENT_CASE("UMOUNT_REQ") {
		// if UMOUNT has been requested for a particular file system, first check
		// whether we have an Index running for that file system; if so, set the
		// "umountRequested" value for that Index to the current user timestamp;
		// the Index is deleted as soon as there are no more queries using it
		char *pathName = tok->getNext();
		printf("UMOUNT requested for %s\n", pathName);
		if (pathName != NULL) {			
			mustReleaseLock = getLock();
			int whichIndex = -1;
			char *path = pathName;
			if (path[strlen(path) - 1] != '/')
				path = concatenateStrings(path, "/");
			for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
				if (mountPoints[i] != NULL)
					if (strcmp(mountPoints[i], path) == 0) {
						whichIndex = i;
						break;
					}
			}
			if (path != pathName)
				free(path);
			if (whichIndex >= 0)
				if ((subIndexes[whichIndex] != NULL) && (unmountRequested[whichIndex] < 0))
					unmountRequested[whichIndex] = registrationID;
			if (mustReleaseLock)
				releaseLock();
		}
	} // end EVENT_CASE("UMOUNT_REQ")

	if ((strcmp(eventType, "WRITE") == 0) || (strcmp(eventType, "TRUNCATE") == 0) ||
	    (strcmp(eventType, "UNLINK") == 0) || (strcmp(eventType, "CHOWN") == 0) ||
	    (strcmp(eventType, "CHMOD") == 0) || (strcmp(eventType, "CREATE") == 0) ||
	    (strcmp(eventType, "MKDIR") == 0) || (strcmp(eventType, "RMDIR") == 0)) {
		// unified event handler for all events that affect data inside an individual
		// index; just find the index responsible for the file system that hosts the
		// file/directory affected and call its notify method
		char *pathName = tok->getNext();
		if (pathName != NULL) {
			mustReleaseLock = getLock();
			int whichMountPoint = getMountPointForPath(pathName);
			if (whichMountPoint >= 0)
				if ((subIndexes[whichMountPoint] == NULL) ||
				    (unmountRequested[whichMountPoint] >= 0))
					whichMountPoint = -1;
			if (mustReleaseLock)
				releaseLock();
			if (whichMountPoint >= 0)
				subIndexes[whichMountPoint]->notify(event);
		}
	} // end if (...)

	EVENT_CASE("RENAME") {
		// distinguish between two cases:
		//   (1) oldPath and newPath lie in the same file system
		//   (2) oldPath and newPath lie in two different file systems
		char *oldPath = tok->getNext();
		char *newPath = tok->getNext();
		if ((oldPath != NULL) && (newPath != NULL)) {
			int whichMountPoint1 = getMountPointForPath(oldPath);
			if (whichMountPoint1 >= 0)
				if ((subIndexes[whichMountPoint1] == NULL) ||
				    (unmountRequested[whichMountPoint1] >= 0))
					whichMountPoint1 = -1;
			int whichMountPoint2 = getMountPointForPath(newPath);
			if (whichMountPoint2 >= 0)
				if ((subIndexes[whichMountPoint2] == NULL) ||
				    (unmountRequested[whichMountPoint2] >= 0))
					whichMountPoint2 = -1;
			if ((whichMountPoint1 == whichMountPoint2) && (whichMountPoint1 >= 0))
				subIndexes[whichMountPoint1]->notify(event);
			else {
				if (whichMountPoint1 >= 0) {
					char *newEvent = concatenateStrings("UNLINK\t", oldPath);
					subIndexes[whichMountPoint1]->notify(newEvent);
					free(newEvent);
				}
				if (whichMountPoint2 >= 0) {
					char *newEvent = concatenateStrings("CREATE\t", newPath);
					subIndexes[whichMountPoint2]->notify(newEvent);
					free(newEvent);
				}
			}
		}
	} // end EVENT_CASE("RENAME")

#undef EVENT_CASE

notify_EXIT:
	free(ev);
	delete tok;
	mustReleaseLock = getLock();
	indexIsBeingUpdated = false;
	if (mustReleaseLock)
		releaseLock();
	return statusCode;
} // end of notify(char*)


DocumentCache * MasterIndex::getDocumentCache(const char *fileName) {
	bool mustReleaseLock = getLock();
	int whichMountPoint = getMountPointForPath(fileName);
	DocumentCache *result =
		(whichMountPoint < 0 ? NULL : subIndexes[whichMountPoint]->getDocumentCache(fileName));
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getDocumentCache(char*)


ExtentList * MasterIndex::getPostings(const char *term, uid_t userID) {
	bool mustReleaseLock = getLock();

	// collect sub-results from all active sub-indexes
	ExtentList **subLists = typed_malloc(ExtentList*, MAX_MOUNT_COUNT);
	offset *relativeOffsets = typed_malloc(offset, MAX_MOUNT_COUNT);
	int subListCount = 0;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
		if ((subIndexes[i] != NULL) && (unmountRequested[i] < 0)) {
			subLists[subListCount] = subIndexes[i]->getPostings(term, userID);
			if (subLists[subListCount] != NULL) {
				if (subLists[subListCount]->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY)
					delete subLists[subListCount];
				else {
					relativeOffsets[subListCount] = i * MAX_INDEX_RANGE_PER_INDEX;
					subListCount++;
				}
			}
		}
	} // end for (int i = 0; i < MAX_MOUNT_COUNT; i++)

	// combine all sub-results into one large ExtentList_OrderedCombination instance
	ExtentList *result;
	if (subListCount == 0) {
		result = new ExtentList_Empty();
		free(subLists);
		free(relativeOffsets);
	}
	else
		result = new ExtentList_OrderedCombination(subLists, relativeOffsets, subListCount);
	
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getPostings(char*, uid_t)


ExtentList * MasterIndex::getPostings(const char *term, uid_t userID, bool fromDisk, bool fromMemory) {
	return new ExtentList_Empty();
}


void MasterIndex::getPostings(const char **terms, int termCount, uid_t userID, ExtentList **results) {
	for (int i = 0; i < termCount; i++)
		results[i] = getPostings(terms[i], userID);
} // end of getPostings(char**, int, uid_t, ExtentList**)


void MasterIndex::addAnnotation(offset position, const char *annotation) {
	LocalLock lock(this);
	int whichIndex = position / MAX_INDEX_RANGE_PER_INDEX;
	position = position % MAX_INDEX_RANGE_PER_INDEX;
	if ((whichIndex < 0) || (whichIndex >= MAX_MOUNT_COUNT))
		return;
	if (subIndexes[whichIndex] != NULL)
		subIndexes[whichIndex]->addAnnotation(position, annotation);
} // end of addAnnotation(offset, char*)


void MasterIndex::getAnnotation(offset position, char *buffer) {
	LocalLock lock(this);
	int whichIndex = position / MAX_INDEX_RANGE_PER_INDEX;
	position = position % MAX_INDEX_RANGE_PER_INDEX;
	if ((whichIndex < 0) || (whichIndex >= MAX_MOUNT_COUNT))
		return;
	if (subIndexes[whichIndex] != NULL)
		subIndexes[whichIndex]->getAnnotation(position, buffer);
} // end of getAnnotation(offset, char*)


void MasterIndex::removeAnnotation(offset position) {
	LocalLock lock(this);
	int whichIndex = position / MAX_INDEX_RANGE_PER_INDEX;
	position = position % MAX_INDEX_RANGE_PER_INDEX;
	if ((whichIndex < 0) || (whichIndex >= MAX_MOUNT_COUNT))
		return;
	if (subIndexes[whichIndex] != NULL)
		subIndexes[whichIndex]->removeAnnotation(position);
} // end of removeAnnotation(offset)


offset MasterIndex::getBiggestOffset() {
	LocalLock lock(this);
	offset result = -1;
	for (int i = MAX_MOUNT_COUNT - 1; i >= 0; i--)
		if (subIndexes[i] != NULL) {
			result = subIndexes[i]->getBiggestOffset();
			result += i * MAX_INDEX_RANGE_PER_INDEX;
			break;
		}
	return result;
} // end of getBiggestOffset()


int MasterIndex::getDocumentType(const char *fullPath) {
	LocalLock lock(this);
	int result = -1;
	int whichIndex = getMountPointForPath(fullPath);
	if (whichIndex >= 0)
		if (subIndexes[whichIndex] != NULL)
			result = subIndexes[whichIndex]->getDocumentType(fullPath);
	return result;
} // end of getDocumentType(char*)


bool MasterIndex::getLastIndexToTextSmallerEq(offset where,
		offset *indexPosition, off_t *filePosition) {
	LocalLock lock(this);
	bool result = false;
	int whichIndex = where / MAX_INDEX_RANGE_PER_INDEX;
	where = where % MAX_INDEX_RANGE_PER_INDEX;
	if ((whichIndex < 0) || (whichIndex >= MAX_MOUNT_COUNT))
		return false;
	if (subIndexes[whichIndex] != NULL) {
		result = subIndexes[whichIndex]->getLastIndexToTextSmallerEq(where, indexPosition, filePosition);
		if (result)
			*indexPosition = *indexPosition + whichIndex * MAX_INDEX_RANGE_PER_INDEX;
	}
	return result;
} // end of getLastIndexToTextSmallerEq(...)


VisibleExtents * MasterIndex::getVisibleExtents(uid_t userID, bool merge) {
	LocalLock lock(this);
	return new MasterVE(this, userID, merge);
} // end of getVisibleExtents(uid_t, bool)


void MasterIndex::getDictionarySize(offset *lowerBound, offset *upperBound) {
	LocalLock lock(this);
	offset lower = 0;
	offset upper = 0;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if (subIndexes[i] != NULL) {
			offset l, u;
			subIndexes[i]->getDictionarySize(&l, &u);
			if (l > lower)
				lower = l;
			upper += u;
		}
	*lowerBound = lower;
	*upperBound = upper;
} // end of getDictionarySize(offset*, offset*)


int64_t MasterIndex::registerForUse(int64_t suggestedID) {
	if (this == NULL)
		return -1;
	if (shutdownInitiated)
		return -1;

	int64_t result = suggestedID + 1;
	if (result < 1)
		result = 1;
	bool mustReleaseLock = getLock();
backToTheBeginning:
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if ((subIndexes[i] != NULL) && (unmountRequested[i] < 0)) {
			int64_t id = subIndexes[i]->registerForUse(result);
			if (id != result) {
				assert(id > result);
				for (int k = 0; k < i; k++)
					if ((subIndexes[k] != NULL) && (unmountRequested[k] < 0))
						subIndexes[k]->deregister(result);
				subIndexes[i]->deregister(id);
				result = id + 1;
				goto backToTheBeginning;
			}
		}
	if (result >= 0)
		registeredUserCount++;
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of registerForUse(int)


void MasterIndex::deregister(int64_t id) {
	// first, do the ordinary deregistration thing
	bool mustReleaseLock = getLock();
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if (subIndexes[i] != NULL)
			subIndexes[i]->deregister(id);
	if (mustReleaseLock)
		releaseLock();

	// once the user has been deregistered, check if any sub-index waits for
	// deletion (UMOUNT_REQ); if that is the case, and the index is not used
	// by any query any more, delete it
	mustReleaseLock = getLock();
	for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
		if (subIndexes[i] != NULL)
			if (unmountRequested[i] >= 0) {
				// we have found an indexed file system for which UMOUNT was requested;
				// check if it is safe to delete the corresponding Index instance
				bool mayDeleteSubIndex = true;
				for (int k = 0; k < registeredUserCount; k++)
					if (registeredUsers[k] < unmountRequested[i]) {
						// "registeredUsers[k] < unmountRequested[i]" means that there is an
						// active query that is still using the sub-index => cannot delete it
						mayDeleteSubIndex = false;
					}
				if (mayDeleteSubIndex) {
					printf("Stopping index for mount point: %s\n", mountPoints[i]);
					delete subIndexes[i];
					subIndexes[i] = NULL;
					unmountRequested[i] = -1;
					indexCount--;
					free(mountPoints[i]);
					mountPoints[i] = NULL;
					activeMountCount--;
					printf("  (active mount points: %d)\n", activeMountCount);
				}
			}
	} // end for (int i = 0; i < MAX_MOUNT_COUNT; i++)
	registeredUserCount--;
	if (mustReleaseLock)
		releaseLock();
} // end of deregister(int)


void MasterIndex::getIndexSummary(char *buffer) {
	LocalLock lock(this);
	int fileCount = 0;
	int directoryCount = 0;
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if (subIndexes[i] != NULL) {
			int deltaFile, deltaDir;
			subIndexes[i]->fileManager->getFileAndDirectoryCount(&deltaFile, &deltaDir);
			fileCount += deltaFile;
			directoryCount += deltaDir;
		}
	buffer += sprintf(buffer, "%d %s\t%d %s\t%d %s\n",
			indexCount, (indexCount == 1 ? "file system" : "file systems"),
			fileCount, (fileCount == 1 ? "file" : "files"),
			directoryCount, (directoryCount == 1 ? "directory" : "directories"));
	for (int i = 0; i < MAX_MOUNT_COUNT; i++) {
		if (subIndexes[i] != NULL) {
			subIndexes[i]->getIndexSummary(buffer);
			buffer = &buffer[strlen(buffer)];
		}
	}
} // end of getIndexSummary(char*)


void MasterIndex::sync() {
	LocalLock lock(this);
	for (int i = 0; i < MAX_MOUNT_COUNT; i++)
		if (subIndexes[i] != NULL)
			subIndexes[i]->sync();
} // end of sync()

