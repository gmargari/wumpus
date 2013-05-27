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
 * Implementation of the Index class.
 *
 * author: Stefan Buettcher
 * created: 2004-09-13
 * changed: 2007-12-17
 **/


#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "index.h"
#include "index_iterator.h"
#include "index_merger.h"
#include "lexicon.h"
#include "multiple_index_iterator.h"
#include "ondisk_index_manager.h"
#include "../daemons/authconn_daemon.h"
#include "../daemons/conn_daemon.h"
#include "../daemons/filesys_daemon.h"
#include "../daemons/query_executor.h"
#include "../extentlist/address_space_transformation.h"
#include "../extentlist/extentlist_transformation.h"
#include "../extentlist/simplifier.h"
#include "../feedback/language_model.h"
#include "../filters/html_inputstream.h"
#include "../filters/inputstream.h"
#include "../filters/multitext_inputstream.h"
#include "../indexcache/docidcache.h"
#include "../indexcache/documentcache.h"
#include "../indexcache/indexcache.h"
#include "../misc/all.h"
#include "../misc/language.h"
#include "../query/gclquery.h"
#include "../stemming/stemmer.h"
#include "../terabyte/terabyte_lexicon.h"


static const char *INDEX_WORKFILE = "index";

const char * Index::TEMP_DIRECTORY = "/tmp";
const char * Index::LOG_ID = "Index";


/** Configuration function. **/
void Index::getConfiguration() {
	getConfigurationInt64("MAX_FILE_SIZE", &MAX_FILE_SIZE, DEFAULT_MAX_FILE_SIZE);
	if (MAX_FILE_SIZE < 32)
		MAX_FILE_SIZE = 32;
	getConfigurationInt64("MIN_FILE_SIZE", &MIN_FILE_SIZE, DEFAULT_MIN_FILE_SIZE);
	if (MIN_FILE_SIZE < 0)
		MIN_FILE_SIZE = 0;
	getConfigurationInt("MAX_UPDATE_SPACE", &MAX_UPDATE_SPACE, DEFAULT_MAX_UPDATE_SPACE);
	if (MAX_UPDATE_SPACE < 16 * 1024 * 1024)
		MAX_UPDATE_SPACE = 16 * 1024 * 1024;
	getConfigurationInt("MAX_SIMULTANEOUS_READERS", &MAX_SIMULTANEOUS_READERS, DEFAULT_MAX_SIMULTANEOUS_READERS);
	if (MAX_SIMULTANEOUS_READERS < 1)
		MAX_SIMULTANEOUS_READERS = 1;
	getConfigurationInt("STEMMING_LEVEL", &STEMMING_LEVEL, DEFAULT_STEMMING_LEVEL);
	getConfigurationBool("BIGRAM_INDEXING", &BIGRAM_INDEXING, DEFAULT_BIGRAM_INDEXING);

	getConfigurationInt("TCP_PORT", &TCP_PORT, DEFAULT_TCP_PORT);
	getConfigurationBool("MONITOR_FILESYSTEM", &MONITOR_FILESYSTEM, DEFAULT_MONITOR_FILESYSTEM);
	getConfigurationBool("ENABLE_XPATH", &ENABLE_XPATH, DEFAULT_ENABLE_XPATH);
	getConfigurationBool("APPLY_SECURITY_RESTRICTIONS", &APPLY_SECURITY_RESTRICTIONS, DEFAULT_APPLY_SECURITY_RESTRICTIONS);
	getConfigurationInt("DOCUMENT_LEVEL_INDEXING", &DOCUMENT_LEVEL_INDEXING, DEFAULT_DOCUMENT_LEVEL_INDEXING);
	getConfigurationDouble("GARBAGE_COLLECTION_THRESHOLD", &garbageThreshold, 0.40);
	getConfigurationDouble("ONTHEFLY_GARBAGE_COLLECTION_THRESHOLD", &onTheFlyGarbageThreshold, 0.25);

	getConfigurationBool("READ_ONLY", &readOnly, false);

	if (!getConfigurationValue("BASE_DIRECTORY", baseDirectory))
		baseDirectory[0] = 0;
} // end of getConfiguration()


Index::Index() {

    //====================================================================
    // gmargari
    //====================================================================
    parse_time.tv_sec = 0;
    parse_time.tv_usec = 0;
    total_time.tv_sec = 0;
    total_time.tv_usec = 0;
    gettimeofday(&total_start_time, NULL);
    gettimeofday(&parse_start_time, NULL);
    //====================================================================

	readOnly = false;
	shutdownInitiated = false;

	// initialize configuration variables from config file
	getConfiguration();
	baseDirectory[0] = 0;

	// check UID (needed for access permissions)
	uid_t uid = getuid();
	uid_t euid = geteuid();
	if (uid != euid) {
		log(LOG_ERROR, LOG_ID, "Error: Index executable must not have the SBIT set.");
		assert(uid == euid);
	}
	indexOwner = uid;

	// initialize sub-systems
	connDaemon = NULL;
	fileSysDaemon = NULL;
	fileManager = NULL;
	registeredUserCount = 0;
	registrationID = 0;
	indexType = TYPE_INDEX;
	indexIsBeingUpdated = false;
	updateOperationsPerformed = 0;
	isConsistent = false;
	cache = NULL;
	documentIDs = NULL;
	documentCache = NULL;

	// create semaphores
	SEM_INIT(registeredUserSemaphore, MAX_REGISTERED_USERS);
	SEM_INIT(updateSemaphore, 1);
} // end of Index()


Index::Index(const char *directory, bool isSubIndex) {

    //====================================================================
    // gmargari
    //====================================================================
    parse_time.tv_sec = 0;
    parse_time.tv_usec = 0;
    total_time.tv_sec = 0;
    total_time.tv_usec = 0;
    gettimeofday(&total_start_time, NULL);
    gettimeofday(&parse_start_time, NULL);
    //====================================================================

	getConfiguration();
	this->isSubIndex = isSubIndex;
	registeredUserCount = 0;
	registrationID = 0;
	SEM_INIT(registeredUserSemaphore, MAX_REGISTERED_USERS);
	SEM_INIT(updateSemaphore, 1);
	indexType = TYPE_INDEX;
	indexIsBeingUpdated = false;
	shutdownInitiated = false;

	// create index directory if necessary; if it cannot be created, or if a file
	// system object with the same name already exists, return with error message
	struct stat statBuf;
	if (stat(directory, &statBuf) != 0) {
		if (readOnly) {
			log(LOG_ERROR, LOG_ID, "Cannot create new index while in read-only mode.");
			exit(1);
		}
		snprintf(errorMessage, sizeof(errorMessage), "Creating index directory: %s", directory);
		log(LOG_DEBUG, LOG_ID, errorMessage);
		mkdir(directory, 0700);
	}
	if (stat(directory, &statBuf) != 0) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to create index directory: %s", directory);
		log(LOG_ERROR, LOG_ID, errorMessage);
		exit(1);
	}
	else if (!S_ISDIR(statBuf.st_mode)) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Object with same name as index directory exists: %s", directory);
		log(LOG_ERROR, LOG_ID, errorMessage);
		exit(1);
	}

	char *disallowFileName = evaluateRelativePathName(directory, ".index_disallow");
	int disallowFile = open(disallowFileName, O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0644);
	if (disallowFile >= 0)
		fchmod(disallowFile, DEFAULT_FILE_PERMISSIONS);
	close(disallowFile);
	free(disallowFileName);

	// initialize garbage collection counters
	usedAddressSpace = 0;
	deletedAddressSpace = 0;
	biggestOffsetSeenSoFar = 0;

	// check UID (needed for access permissions)
	uid_t uid = getuid();
	uid_t euid = geteuid();
	if (uid != euid) {
		log(LOG_ERROR, LOG_ID, "Index executable must not have the SBIT set.");
		exit(1);
	}
	indexOwner = uid;

	this->directory = duplicateString(directory);
	char *fileName = evaluateRelativePathName(directory, INDEX_WORKFILE);

	struct stat fileInfo;
	if (lstat(fileName, &fileInfo) == 0) {
		loadDataFromDisk();
		if (!isConsistent) {
			snprintf(errorMessage, sizeof(errorMessage),
					"On-disk index found in inconsistent state: %s. Creating new index.", directory);
			log(LOG_DEBUG, LOG_ID, errorMessage);
			DIR *dir = opendir(directory);
			struct dirent *child;
			while ((child = readdir(dir)) != NULL) {
				if (child->d_name[0] == '.')
					continue;
				char *fn = evaluateRelativePathName(directory, child->d_name);
				struct stat buf;
				if (lstat(fn, &buf) == 0) {
					// As a precaution, delete only files having file permission
					// "-rw-------". This is in order to avoid too many files get deleted
					// if somebody tries to create an index in a directory that contains
					// other information as well...
					if ((buf.st_mode & 0777) == DEFAULT_FILE_PERMISSIONS)
						if ((S_ISLNK(buf.st_mode)) || (S_ISREG(buf.st_mode)) || (S_ISFIFO(buf.st_mode)))
							unlink(fn);
				}
				free(fn);
			}
			closedir(dir);
		}
	} // end if (lstat(fileName, &fileInfo) == 0)

	bool createFromScratch;
	if (lstat(fileName, &fileInfo) == 0) {
		// INDEX_WORKFILE does exist: load data from disk
		loadDataFromDisk();
		createFromScratch = false;
	}
	else {
		if (readOnly) {
			log(LOG_ERROR, LOG_ID, "Cannot create new index while in read-only mode.");
			exit(1);
		}

		// INDEX_WORKFILE does not exist: create the entire structure
		updateOperationsPerformed = 0;
		isConsistent = true;
		int fd = open(fileName, O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE, DEFAULT_FILE_PERMISSIONS);
		if (fd < 0) {
			snprintf(errorMessage, sizeof(errorMessage), "Unable to create index: %s.", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			assert(fd >= 0);
		}
		close(fd);
		createFromScratch = true;
	}

	fileManager = new FileManager(this, directory, createFromScratch);
	securityManager = new SecurityManager(fileManager);
	indexManager = new OnDiskIndexManager(this);
	indexToTextMap = new IndexToText(directory, createFromScratch);
	documentIDs = new DocIdCache(directory, true);

//	XXX annotator disabled for now because FileSystem is incompatible with
//	FAT32 (no truncate)
//	annotator = new Annotator(directory, createFromScratch);
	annotator = NULL;

	saveDataToDisk();
	free(fileName);

	char *docCacheDir = evaluateRelativePathName(directory, "cache");
	documentCache = new DocumentCache(docCacheDir);
	free(docCacheDir);
	cache = new IndexCache(this);
	invalidateCacheContent();

	if (baseDirectory[0] != 0)
		fileManager->setMountPoint(baseDirectory);

	if (!isSubIndex) {
		if (TCP_PORT >= 0) {
			connDaemon = new ConnDaemon(this, TCP_PORT);
			connDaemon->start();
		}
		else
			connDaemon = NULL;
		if (Index::MONITOR_FILESYSTEM) {
			if (baseDirectory[0] != 0)
				fileSysDaemon = new FileSysDaemon(this, baseDirectory);
			else
				fileSysDaemon = new FileSysDaemon(this, NULL);
			if (!fileSysDaemon->stopped())
				fileSysDaemon->start();
		}
		else
			fileSysDaemon = NULL;
	}
	else {
		connDaemon = NULL;
		fileSysDaemon = NULL;
		baseDirectory[0] = 0;
	}

} // end of Index(char*)


Index::~Index() {
	bool mustReleaseLock;

	shutdownInitiated = true;

	if (indexType == TYPE_INDEX) {
		// stop all daemons
		if (fileSysDaemon != NULL) {
			delete fileSysDaemon;
			fileSysDaemon = NULL;
		}
		if (connDaemon != NULL) {
			delete connDaemon;
			connDaemon = NULL;
		}

		// delete the index manager; this will wait for all running processes
		// (active Query instances) to finish
		mustReleaseLock = getLock();
		while (indexIsBeingUpdated) {
			releaseLock();
			waitMilliSeconds(100);
			getLock();
		}
		OnDiskIndexManager *manager = indexManager;
		indexManager = NULL;
		if (mustReleaseLock)
			releaseLock();
		delete manager;

		mustReleaseLock = getLock();

		delete securityManager;
		delete indexToTextMap;
		if (annotator != NULL) {
			delete annotator;
			annotator = NULL;
		}

		if (!readOnly)
			fileManager->saveToDisk();
		isConsistent = true;
		saveDataToDisk();
		free(directory);
		delete fileManager;
		fileManager = NULL;

		if (cache != NULL) {
			delete cache;
			cache = NULL;
		}
		if (documentIDs != NULL) {
			delete documentIDs;
			documentIDs = NULL;
		}
		if (documentCache != NULL) {
			delete documentCache;
			documentCache = NULL;
		}

		if (mustReleaseLock)
			releaseLock();
	} // end if (indexType == TYPE_INDEX)

	sem_destroy(&registeredUserSemaphore);
	sem_destroy(&updateSemaphore);
} // end of ~Index()


void Index::sync() {
	LocalLock lock(this);
	if (!isConsistent) {
	  if (indexToTextMap != NULL)
			indexToTextMap->saveToDisk();
	  if (fileManager != NULL)
			fileManager->saveToDisk();
		if (indexManager != NULL)
			indexManager->sync();
		isConsistent = true;
		saveDataToDisk();
	}
} // end of sync()


void Index::invalidateCacheContent() {
	if (cache == NULL)
		return;
	cache->invalidate();
} // end of invalidateCacheContent()


IndexCache * Index::getCache() {
	return cache;
}


DocumentCache * Index::getDocumentCache(const char *fileName) {
	return documentCache;
}


ExtentList * Index::getCachedList(const char *queryString) {
	bool mustReleaseLock = getLock();
	ExtentList *result = NULL;
	if (cache != NULL)
		result = cache->getCachedList(queryString);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getCachedList(char*)


void Index::loadDataFromDisk() {
	char *fileName = evaluateRelativePathName(directory, INDEX_WORKFILE);
	FILE *f = fopen(fileName, "r");
	free(fileName);
	if (f == NULL) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to open index: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		exit(1);
	}
	char line[1024];
	STEMMING_LEVEL = -1;
	while (fgets(line, 1022, f) != NULL) {
		if (strlen(line) > 1)
			while (line[strlen(line) - 1] == '\n')
				line[strlen(line) - 1] = 0;
		if (startsWith(line, "STEMMING_LEVEL = "))
			sscanf(&line[strlen("STEMMING_LEVEL = ")], "%d", &STEMMING_LEVEL);
		if (startsWith(line, "BIGRAM_INDEXING = "))
			BIGRAM_INDEXING = (strcasecmp(&line[strlen("BIGRAM_INDEXING = ")], "true") == 0);
		if (startsWith(line, "UPDATE_OPERATIONS = "))
			sscanf(&line[strlen("UPDATE_OPERATIONS = ")], "%d", &updateOperationsPerformed);
		if (startsWith(line, "IS_CONSISTENT = ")) {
			if (strcasecmp(&line[strlen("IS_CONSISTENT = ")], "true") == 0)
				isConsistent = true;
			else if (strcasecmp(&line[strlen("IS_CONSISTENT = ")], "false") == 0)
				isConsistent = false;
		}
		if (startsWith(line, "DOCUMENT_LEVEL_INDEXING = "))
			sscanf(&line[strlen("DOCUMENT_LEVEL_INDEXING = ")], "%d", &DOCUMENT_LEVEL_INDEXING);
		if (startsWith(line, "USED_ADDRESS_SPACE = "))
			sscanf(&line[strlen("USED_ADDRESS_SPACE = ")], OFFSET_FORMAT, &usedAddressSpace);
		if (startsWith(line, "DELETED_ADDRESS_SPACE = "))
			sscanf(&line[strlen("DELETED_ADDRESS_SPACE = ")], OFFSET_FORMAT, &deletedAddressSpace);
		if (startsWith(line, "BIGGEST_OFFSET = "))
			sscanf(&line[strlen("BIGGEST_OFFSET = ")], OFFSET_FORMAT, &biggestOffsetSeenSoFar);
	}
	if ((STEMMING_LEVEL < 0) || (STEMMING_LEVEL > 3)) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Illegal configuration values in index file: %s", directory);
		log(LOG_ERROR, LOG_ID, errorMessage);
		exit(1);
	}
	fclose(f);
} // end of loadDataFromDisk()


void Index::saveDataToDisk() {
	if (readOnly)
		return;

	// save internal data to disk; be a bit verbose so that a human can
	// read the stuff and actually extract some information
	char *fileName = evaluateRelativePathName(directory, INDEX_WORKFILE);
	int fd = open(fileName,
			O_RDWR | O_CREAT | O_TRUNC | O_SYNC | O_LARGEFILE, DEFAULT_FILE_PERMISSIONS);
	free(fileName);
	if (fd < 0) {
		snprintf(errorMessage, sizeof(errorMessage), "Error: Could not create %s\n", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		assert(fd >= 0);
	}
	FILE *f = fdopen(fd, "w");
	fprintf(f, "# This is a Wumpus index file. Unless you know exactly what you are doing,\n");
	fprintf(f, "# please do not modify any of the information stored in this file.\n\n");
	fprintf(f, "STEMMING_LEVEL = %d\n", STEMMING_LEVEL);
	fprintf(f, "BIGRAM_INDEXING = %s\n", BIGRAM_INDEXING ? "true" : "false");
	fprintf(f, "DIRECTORY_COUNT = %d\n", fileManager->directoryCount);
	fprintf(f, "FILE_COUNT = %d\n", fileManager->fileCount);
	fprintf(f, "INODE_COUNT = %d\n", fileManager->iNodeCount);
	fprintf(f, "UPDATE_OPERATIONS = %d\n", updateOperationsPerformed);
	fprintf(f, "DOCUMENT_LEVEL_INDEXING = %d\n", DOCUMENT_LEVEL_INDEXING);
	fprintf(f, "USED_ADDRESS_SPACE = " OFFSET_FORMAT "\n", usedAddressSpace);
	fprintf(f, "DELETED_ADDRESS_SPACE = " OFFSET_FORMAT "\n", deletedAddressSpace);
	fprintf(f, "BIGGEST_OFFSET = " OFFSET_FORMAT "\n", biggestOffsetSeenSoFar);
	fprintf(f, "IS_CONSISTENT = %s\n", isConsistent ? "true" : "false");
	fclose(f);
} // end of saveDataToDisk()


void Index::markAsInconsistent() {
	if (isConsistent) {
		isConsistent = false;
		saveDataToDisk();
	}
} // end of markAsInconsistent()


int Index::notify(const char *event) {
	snprintf(errorMessage, sizeof(errorMessage), "Event received: \"%s\"", event);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	// first, make sure no other process is updating the index at the same time
	sem_wait(&updateSemaphore);
	bool mustReleaseLock = getLock();
	if (registrationID < 0) {
		if (mustReleaseLock)
			releaseLock();
		return ERROR_SHUTTING_DOWN;
	}
	if (indexIsBeingUpdated) {
		if (mustReleaseLock)
			releaseLock();
		return ERROR_CONCURRENT_UPDATE;
	}
	if (readOnly) {
		if (mustReleaseLock)
			releaseLock();
		return ERROR_READ_ONLY;
	}
	indexIsBeingUpdated = true;
	if (mustReleaseLock)
		releaseLock();

	char *eventType;
	int eventHash;
	double currentGarbage;
	StringTokenizer *tok = new StringTokenizer(event, "\t");
	int statusCode = RESULT_SUCCESS;

	if (!tok->hasNext()) {
		statusCode = ERROR_SYNTAX_ERROR;
		goto notify_EXIT;
	}
	eventType = tok->getNext();

#define EVENT_CASE(S) if (strcmp(eventType, S) == 0)
#define EVENT_CASE_2(S1, S2) if ((strcmp(eventType, S1) == 0) || (strcmp(eventType, S2) == 0))

	EVENT_CASE("MOUNT") {
		// ignore
	}

	EVENT_CASE("UMOUNT") {
		// ignore
	}

	EVENT_CASE("UMOUNT_REQ") {
		// ignore
	}

	EVENT_CASE_2("WRITE", "CREATE") {
		char *fileName = tok->getNext();
		char *startOffset = tok->getNext();
		char *endOffset = tok->getNext();
		char *fileType = tok->getNext();
		statusCode = ERROR_NO_SUCH_FILE;
		struct stat buf;
		if ((stat(fileName, &buf) == 0) && (fileName[0] == '/')) {
			if (S_ISREG(buf.st_mode)) {
				if ((directoryAllowed(fileName)) && (startsWith(fileName, baseDirectory))) {
					if (fileManager->changedSinceLastUpdate(fileName)) {
						markAsInconsistent();
						mustReleaseLock = getLock();
						fileManager->removeFile(fileName);
						if (mustReleaseLock)
							releaseLock();
						statusCode = addFile(fileName, fileType);
					}
					else
						statusCode = ERROR_FILE_UNCHANGED;
				}
				else {
					markAsInconsistent();
					mustReleaseLock = getLock();
					fileManager->removeFile(fileName);
					if (mustReleaseLock)
						releaseLock();
					statusCode = ERROR_DIR_NOT_ALLOWED;
				}
			}
		}
	} // end EVENT_CASE_2("WRITE", "CREATE")

	EVENT_CASE("APPEND") {
		char *fileName = tok->getNext();
		statusCode = ERROR_NO_SUCH_FILE;
		struct stat buf;
		if ((stat(fileName, &buf) == 0) && (fileName[0] == '/')) {
			if (S_ISREG(buf.st_mode)) {
				if ((directoryAllowed(fileName)) && (startsWith(fileName, baseDirectory))) {
					if (fileManager->changedSinceLastUpdate(fileName)) {
						markAsInconsistent();
						statusCode = addFile(fileName, NULL);
					}
					else
						statusCode = ERROR_FILE_UNCHANGED;
				}
				else {
					markAsInconsistent();
					mustReleaseLock = getLock();
					fileManager->removeFile(fileName);
					if (mustReleaseLock)
						releaseLock();
					statusCode = ERROR_DIR_NOT_ALLOWED;
				}
			}
		}
	} // end EVENT_CASE("APPEND")

	EVENT_CASE("TRUNCATE") {
		char *fileName = tok->getNext();
		if (startsWith(fileName, baseDirectory)) {
			const char *fileSize = tok->getNext();
			if (fileSize == NULL)
				fileSize = "0";
			if ((!directoryAllowed(fileName)) || (strcmp(fileSize, "0") == 0)) {
				markAsInconsistent();
				mustReleaseLock = getLock();
				bool status = fileManager->removeFile(fileName);
				if (mustReleaseLock)
					releaseLock();
				if (status)
					updateOperationsPerformed++;
			}
			else if (fileManager->changedSinceLastUpdate(fileName)) {
				markAsInconsistent();
				mustReleaseLock = getLock();
				fileManager->removeFile(fileName);
				if (mustReleaseLock)
					releaseLock();
				addFile(fileName, NULL);
			}
		}
	}

	EVENT_CASE("RENAME") {
		char *oldPath = tok->getNext();
		char *newPath = tok->getNext();
		if (!fileManager->renameFileOrDirectory(oldPath, newPath)) {
			// negative return value can mean different things; one possibility
			// is that "oldPath" was not inside the index ... hmm ... so, let's
			// just index "newPath" then!
			char *event1 = concatenateStrings("UNLINK\t", oldPath);
			char *event2 = concatenateStrings("CREATE\t", newPath);
			delete tok;
			mustReleaseLock = getLock();
			indexIsBeingUpdated = false;
			if (mustReleaseLock)
				releaseLock();
			sem_post(&updateSemaphore);
			statusCode = notify(event1);
			free(event1);
			if (statusCode == RESULT_SUCCESS)
				statusCode = notify(event2);
			free(event2);
			return statusCode;
		}
		else
			markAsInconsistent();
	} // end EVENT_CASE("RENAME")

	EVENT_CASE("UNLINK") {
		mustReleaseLock = getLock();
		char *fileName = tok->getNext();
		bool status = fileManager->removeFile(fileName);
		if (status) {
			statusCode = RESULT_SUCCESS;
			markAsInconsistent();
			updateOperationsPerformed++;
		}
		else
			statusCode = RESULT_ERROR;
		if (mustReleaseLock)
			releaseLock();
	}

	EVENT_CASE_2("CHMOD", "CHOWN") {
		char *fileName = tok->getNext();
		char *modeString = tok->getNext();
		if ((startsWith(fileName, baseDirectory)) && (directoryAllowed(fileName))) {
			struct stat buf;
			if (stat(fileName, &buf) == 0) {
				markAsInconsistent();
				if (S_ISDIR(buf.st_mode))
					fileManager->updateDirectoryAttributes(fileName);
				else
					fileManager->updateFileAttributes(fileName);
			}
		}
	} // end EVENT_CASE_2("CHMOD", "CHOWN")

	EVENT_CASE("MKDIR") {
		// ignore
	}

	EVENT_CASE("RMDIR") {
		mustReleaseLock = getLock();
		markAsInconsistent();
		char *dirName = tok->getNext();
		fileManager->removeDirectory(dirName);
		if (mustReleaseLock)
			releaseLock();
	}

#undef EVENT_CASE

notify_EXIT:

	delete tok;
	mustReleaseLock = getLock();
	indexIsBeingUpdated = false;
	if (mustReleaseLock)
		releaseLock();
	sem_post(&updateSemaphore);
	deregister(-1);
	return statusCode;
} // end of notify(char*)


void Index::notifyOfAddressSpaceChange(int signum, offset start, offset end) {
	if (end < start)
		return;
	LocalLock lock(this);

	if (signum > 0)
		usedAddressSpace += (end - start + 1);
	else if (signum < 0)
		deletedAddressSpace += (end - start + 1);
	else {
		sprintf(errorMessage, "notifyOfAddressSpaceChange(%d, %lld, %lld)",
				signum, static_cast<long long>(start), static_cast<long long>(end));
		log(LOG_ERROR, LOG_ID, errorMessage);
	}

#if 0
	sprintf(errorMessage, "usedAddressSpace = %lld, deletedAddressSpace = %lld",
			usedAddressSpace, deletedAddressSpace);
	log(LOG_DEBUG, LOG_ID, errorMessage);
#endif

	// propagate information to index manager so that it can be used to decide
	// when to perform on-the-fly garbage collection and when not
	if (indexManager != NULL)
		indexManager->notifyOfAddressSpaceChange(signum, start, end);

	// check whether we need to run garbage collector immediately
	if ((securityManager == NULL) && (deletedAddressSpace < MIN_GARBAGE_COLLECTION_THRESHOLD))
		return;
	if (deletedAddressSpace < usedAddressSpace * garbageThreshold)
		return;

	// run garbage collection for all helper data structures; running it on posting
	// lists is not necessary, because this is already taken care of by the
	// index manager (via indexManager->notifyOfAddressSpaceChange)
	VisibleExtents *visible = getVisibleExtents(SUPERUSER, true);
	ExtentList *list = visible->getExtentList();
	documentIDs->filterAgainstFileList(list);
	indexToTextMap->filterAgainstFileList(list);
	delete list;
	delete visible;
	usedAddressSpace -= deletedAddressSpace;
	deletedAddressSpace = 0;
} // end of notifyOfAddressSpaceChange(int, offset, offset)


void Index::addPostingsToIndex(InputToken *tokens, int tokenCount) {
	indexManager->addPostings(tokens, tokenCount);
	if (BIGRAM_INDEXING) {
		// if bigram indexing is turned on, we need to insert special tokens that
		// correspond to the bigram sequence defined by the token sequence
		while (tokenCount > 0) {
			static const int MAX_BIGRAM_COUNT = 64;
			char buffer[MAX_BIGRAM_COUNT * (MAX_TOKEN_LENGTH + 1)];
			char *bigrams[MAX_BIGRAM_COUNT];
			offset postings[MAX_BIGRAM_COUNT];
			int bigramCount = 0, bufferPos = 0;
			while ((tokenCount > 0) && (bigramCount < MAX_BIGRAM_COUNT)) {
				if (previousToken.posting == tokens[0].posting - 1) {
					char bigram[MAX_TOKEN_LENGTH * 4];
					sprintf(bigram, "%s_%s", (char*)previousToken.token, (char*)tokens[0].token);
					int len = strlen(bigram);
					if (len <= MAX_TOKEN_LENGTH) {
						bigrams[bigramCount] = &buffer[bufferPos];
						strcpy(bigrams[bigramCount], bigram);
						postings[bigramCount] = previousToken.posting;
						bigramCount++;
						bufferPos += len + 1;
					}
				}
				if ((tokens[0].token[0] != '<') ||
				    (strchr((char*)tokens[0].token, '!') == NULL)) {
					// There is a bad interaction between XPath support and bigram
					// indexing. We need to make sure the bigram we're indexing contains
					// the real token, and not a fake XPath token (e.g., "<level!2>");
					memcpy(&previousToken, tokens, sizeof(previousToken));
				}
				tokens++;
				tokenCount--;
			}
			if (bigramCount > 0)
				indexManager->addPostings(bigrams, postings, bigramCount);
		}
	}
} // end of addPostingsToIndex(InputToken*, int)


int Index::addFile(const char *fileName, const char *fileType) {
	bool mustReleaseLock;
	struct stat fileInfo;
	if (shutdownInitiated)
		return ERROR_SHUTTING_DOWN;
	if (stat(fileName, &fileInfo) != 0)
		return ERROR_NO_SUCH_FILE;
	if (fileInfo.st_size < MIN_FILE_SIZE)
		return ERROR_FILE_TOO_SMALL;
	if (fileInfo.st_size > MAX_FILE_SIZE)
		return ERROR_FILE_TOO_LARGE;
	int fileDescriptor = open(fileName, O_RDONLY);
	if (fileDescriptor < 0)
		return ERROR_ACCESS_DENIED;
	close(fileDescriptor);

	char *newFileName = duplicateString(fileName);
	collapsePath(newFileName);

	// file preprocessing, which is done inside the getInputStream method, is
	// not a critical section, so we do not need a lock here
	FilteredInputStream *inputStream = FilteredInputStream::getInputStream(
			newFileName, FilteredInputStream::stringToDocumentType(fileType),
			getDocumentCache(newFileName));
	if (inputStream == NULL) {
		// No input filter found. File cannot be processed.
		free(newFileName);
		return ERROR_UNKNOWN_FILE_FORMAT;
	}

	char *documentType = inputStream->documentTypeToString(inputStream->getDocumentType());
	snprintf(errorMessage, sizeof(errorMessage),
			"InputStream created for \"%s\": %s", fileName, documentType);
	free(documentType);
	log(LOG_OUTPUT, LOG_ID, errorMessage);

	indexManager->updateIndex->setInputStream(inputStream);

	uint32_t tokenCount = 0;
	int tppSlots = 1024;
	int tppCount = 0;
	uint32_t lastTokenInList = 0;
	off_t lastFilePosInList = 0;
	uint32_t lastSequenceNumber = 0;
	int currentXmlLevel = 0;
	bool lastTokenWasCloseDoc = false;
	TokenPositionPair *tokenPositionPairs = typed_malloc(TokenPositionPair, tppSlots);

	offset startOffset = -1;
	uint32_t reservedTokenCount = 4000000000LL;

#if SUPPORT_APPEND_TAQT
	IndexedINodeOnDisk iiod;
	if (fileManager->getINodeInfo(newFileName, &iiod)) {
		offset oldStartOffset = iiod.coreData.startInIndex;
		offset initialTokenCount = AddressSpaceTransformation::getInitialTokenCount(oldStartOffset);
		if ((iiod.coreData.tokenCount > 3 * initialTokenCount) && (false)) {
			AddressSpaceTransformation::removeRules(oldStartOffset);
		}
		else if (inputStream->seekToFilePosition(iiod.fileSize, iiod.coreData.tokenCount)) {
			fileManager->removeFile(newFileName);
			startOffset =
				fileManager->addFile(newFileName, inputStream->getDocumentType(), LANGUAGE_NONE);
			tokenCount = iiod.coreData.tokenCount;

			// We need to do two things here:
			// 1. update all transformation rules that map into the old file position;
			// 2. add a new rule that maps everything from the old to the new position.
			AddressSpaceTransformation::updateRules(oldStartOffset, startOffset, tokenCount);
		}
	}
#endif // SUPPORT_APPEND_TAQT

#if SUPPORT_APPEND_TAIT
	// Indexing-time transformation of incoming postings; simply insert into
	// update index after the existing postings for the current file.
	// Further down in this method, we need to check whether we have just run out
	// of address space for the current file; when that happens, remove file and
	// re-index.
	IndexedINodeOnDisk iiod;
	if (fileManager->getINodeInfo(newFileName, &iiod)) {
		startOffset = iiod.coreData.startInIndex;
		if (!inputStream->seekToFilePosition(iiod.fileSize, iiod.coreData.tokenCount))
			startOffset = -1;
		else {
			tokenCount = iiod.coreData.tokenCount;
			reservedTokenCount = iiod.reservedTokenCount;
		}
	}
#endif

	if (startOffset < 0) {
		// if startOffset is still smaller than zero, that means we are not doing an
		// append operation here; everything back to normal!
		fileManager->removeFile(newFileName);
		startOffset = 
			fileManager->addFile(newFileName, inputStream->getDocumentType(), LANGUAGE_NONE);
		if ((startOffset < fileManager->biggestOffset) || (startOffset < 0)) {
			delete inputStream;
			free(newFileName);
			return ERROR_INTERNAL_ERROR;
		}
	}

	// last sequence number at which we entered a new XML level, only
	// used if ENABLE_XPATH is true
	int64_t lastXPathSequenceNumber = -1;

	// we use a token buffer to batch the addPosting calls, since otherwise the
	// locking inside the Lexicon would be too expensive
	static const int TOKEN_BUFFER_SIZE = 8192;
	InputToken tokenBuffer[TOKEN_BUFFER_SIZE];
	int tokenBufferPos = 0;

	// apply the updates to the Lexicon in batches; this increases both sequential
	// performance and the ability to do stuff in parallel
	#define ADD_POSTING(term, offset) { \
		strcpy((char*)tokenBuffer[tokenBufferPos].token, term); \
		tokenBuffer[tokenBufferPos].hashValue = Lexicon::getHashValue(term); \
		tokenBuffer[tokenBufferPos].posting = offset; \
		tokenBufferPos++; \
	}

	// tells us at what "tokenCount" value we did the last update to the file/INode
	// information inside the FileManager
	bool docnoSeenForCurrentDoc = false;
	offset lastDocStart = -1;
	int statusCode = RESULT_SUCCESS;

	// process all tokens in the input stream
	while (inputStream->getNextToken(&tokenBuffer[tokenBufferPos])) {
		tokenBuffer[tokenBufferPos].posting =
			startOffset + tokenBuffer[tokenBufferPos].sequenceNumber;
		unsigned int hashValue = tokenBuffer[tokenBufferPos].hashValue =
			Lexicon::getHashValue((char*)tokenBuffer[tokenBufferPos].token);
		off_t filePosition = tokenBuffer[tokenBufferPos].filePosition;
		unsigned int sequenceNumber = tokenBuffer[tokenBufferPos].sequenceNumber;
		tokenCount = sequenceNumber + 1;

		// keep track of "<DOC>" and "<DOCNO>" tags; it's a bit painful, but we have
		// to do this if we want to be able to efficiently look up TREC-style document
		// IDs for document start positions; since it is not Lexicon-specific, we have
		// to put it here...
		if (hashValue == startDocHashValue) {
			if (strcmp((char*)tokenBuffer[tokenBufferPos].token, START_OF_DOCUMENT_TAG) == 0) {
				lastDocStart = startOffset + sequenceNumber;
				docnoSeenForCurrentDoc = false;
			}
		}
		else if ((hashValue == endDocnoHashValue) && (!docnoSeenForCurrentDoc)) {
			if (strcmp((char*)tokenBuffer[tokenBufferPos].token, END_OF_DOCNO_TAG) == 0) {
				if ((lastDocStart >= 0) && (documentIDs != NULL)) {
					char buffer[40];
					inputStream->getPreviousChars(buffer, sizeof(buffer) - 1);
					buffer[0] = '>';
					buffer[sizeof(buffer) - 1] = 0;
					int start = sizeof(buffer) - 1;
					while (--start > 0) {
						if (buffer[start] == '<') {
							buffer[start] = 0;
							break;
						}
					}
					while (buffer[--start] == ' ')
						buffer[start] = 0;
					while (buffer[--start] != '>');
					start++;
					if (start > 1) {
						while (buffer[start] == ' ')
							start++;
						documentIDs->addDocumentID(lastDocStart, &buffer[start]);
						lastDocStart = -1;
					}
				}
				docnoSeenForCurrentDoc = true;
			}
		} // end if (tokenBuffer[tokenBufferPos].hashValue == endDocnoHashValue)

		tokenBufferPos++;

		// specal handling for XML nesting information
		if (ENABLE_XPATH) {
			// first, make sure that we leave level 0 at the beginning
			if (currentXmlLevel == 0) {
				ADD_POSTING("<level!0>", startOffset);
				currentXmlLevel++;
			}

			// then, process the actual XML tag
			if ((lastXPathSequenceNumber < sequenceNumber) &&
			    (tokenBuffer[tokenBufferPos - 1].token[0] == '<')) {
				lastXPathSequenceNumber = sequenceNumber;

				char tok[32];
				switch (tokenBuffer[tokenBufferPos - 1].token[1]) {
					case '!':
					case '?':
						// special tag: do nothing
						break;
					case '/':
						// closing tag: decrease nesting level
						currentXmlLevel--;
						sprintf(tok, "</level!%i>", currentXmlLevel);
						ADD_POSTING(tok, startOffset + sequenceNumber);
						break;
					default:
						// opening tag: increase nesting level
						sprintf(tok, "<level!%i>", currentXmlLevel);
						ADD_POSTING(tok, startOffset + sequenceNumber);
						currentXmlLevel++;
				}
			}
		} // end if (ENABLE_XPATH)

		// check if it is time for another (sequenceNumber,filePosition) pair
		// that is necessary for efficient @get queries
		if ((tokenCount > lastTokenInList + INDEX_TO_TEXT_GRANULARITY) ||
		    (filePosition > lastFilePosInList + 65536)) {
			if (tokenBuffer[tokenBufferPos - 1].canBeUsedAsLandmark)
				if (sequenceNumber > lastSequenceNumber) {
					if (tppCount >= tppSlots) {
						tppSlots *= 2;
						typed_realloc(TokenPositionPair, tokenPositionPairs, tppSlots);
					}
					tokenPositionPairs[tppCount].sequenceNumber = sequenceNumber;
					tokenPositionPairs[tppCount].filePosition = filePosition;
					tppCount++;
					lastTokenInList = tokenCount;
					lastFilePosInList = tokenBuffer[tokenBufferPos - 1].filePosition;
				}
		}

		if (tokenBufferPos >= TOKEN_BUFFER_SIZE - 32) {
#if SUPPORT_APPEND_TAIT
			// if we have come to the point where the address space reserved for the
			// current file is insufficient to hold the incoming postings, we need to
			// re-index the whole thing
			if (tokenCount > reservedTokenCount) {
				snprintf(errorMessage, sizeof(errorMessage),
						"Running out of address space for \"%s\". Re-indexing.", newFileName);
				log(LOG_DEBUG, LOG_ID, errorMessage);
				fileManager->removeFile(newFileName);
				statusCode = addFile(fileName, fileType);
				goto addFile_END;
			}
#endif

			// update the amount of address space reserved for this file
			fileManager->changeTokenCount(newFileName, tokenCount, 0);

			if (tokenBuffer[0].posting <= startOffset + 1) {
				// in order to be able to rank everything in a uniform way, we insert
				// "<document!>" and "</document!>" tags into the index, one at the beginning
				// and one at the end of the file; however, we have to be a bit careful because
				// MBoxInputStream, for example, inserts "<document!>" tags itself; so, we
				// only do this if there are no tags present yet
				bool docTagFound = false;
				for (int i = 0; (i < tokenBufferPos) && (tokenBuffer[i].posting <= startOffset + 1); i++) {
					if (strcmp((char*)tokenBuffer[i].token, "<document!>") == 0) {
						docTagFound = true;
						break;
					}
				}
				if (!docTagFound)
					ADD_POSTING("<document!>", startOffset);
			} // end if (postingBuffer[0] == startOffset)

			if (shutdownInitiated)
				break;

			mustReleaseLock = getLock();
			addPostingsToIndex(tokenBuffer, tokenBufferPos);
			lastTokenWasCloseDoc =
				(strcmp((char*)tokenBuffer[tokenBufferPos - 1].token, "</document!>") == 0);
			tokenBufferPos = 0;

			if (mustReleaseLock)
				releaseLock();
		} // end if (bufferPos >= TOKEN_BUFFER_SIZE - 32)

		lastSequenceNumber = sequenceNumber;
	} // end while (inputStream->getNextToken(&tokenBuffer[tokenBufferPos]))

	if (tokenCount > 0) {
#if SUPPORT_APPEND_TAIT
		if (tokenCount > reservedTokenCount) {
			// if we have come to the point where the address space reserved for the
			// current file is insufficient to hold the incoming postings, we need to
			// re-index the whole thing
			snprintf(errorMessage, sizeof(errorMessage),
					"Running out of address space for \"%s\". Re-indexing.", newFileName);
			log(LOG_DEBUG, LOG_ID, errorMessage);
			fileManager->removeFile(newFileName);
			statusCode = addFile(fileName, fileType);
			goto addFile_END;
		}
		if (reservedTokenCount == 4000000000LL)
			fileManager->changeTokenCount(newFileName, tokenCount, tokenCount * 3);
		else
			fileManager->changeTokenCount(newFileName, tokenCount, 0);
#else
		fileManager->changeTokenCount(newFileName, tokenCount, 0);
#endif
	}

	if (tokenBufferPos > 0) {
		if (tokenBuffer[0].posting <= startOffset + 1) {
			// in order to be able to rank everything in a uniform way, we insert
			// "<document!>" and "</document!>" tags into the index, one at the beginning
			// and one at the end of the file; however, we have to be a bit careful because
			// MBoxInputStream, for example, inserts "<document!>" tags itself; so, we
			// only do this if there are no tags present yet
			bool docTagFound = false;
			for (int i = 0; (i < tokenBufferPos) && (tokenBuffer[i].posting <= startOffset + 1); i++) {
				if (strcmp((char*)tokenBuffer[i].token, "<document!>") == 0) {
					docTagFound = true;
					break;
				}
			}
			if (!docTagFound)
				ADD_POSTING("<document!>", startOffset);
		} // end if (postingBuffer[0] == startOffset)

		mustReleaseLock = getLock();
		addPostingsToIndex(tokenBuffer, tokenBufferPos);
		lastTokenWasCloseDoc =
			(strcmp((char*)tokenBuffer[tokenBufferPos - 1].token, "</document!>") == 0);
		tokenBufferPos = 0;
		if (mustReleaseLock)
			releaseLock();
	} // end if (bufferPos > 0)

	// update the "biggestOffsetSeenSoFar" in order to avoid that the garbage
	// collector tries to collect inexistent postings
	if (startOffset + lastSequenceNumber > biggestOffsetSeenSoFar)
		biggestOffsetSeenSoFar = startOffset + lastSequenceNumber;

	// finish the operation
	mustReleaseLock = getLock();

	if (tokenCount == 0) {
		fileManager->removeFile(newFileName);
		startOffset = -1;
		statusCode = ERROR_EMPTY_FILE;
	}
	else {
		// if we have XPath indexing enabled, close the topmost container before leaving
		if (ENABLE_XPATH)
			ADD_POSTING("</level!0>", startOffset + lastSequenceNumber);

		// add the TokenPositionPair information to some global data pool somehow...
		if (tppCount > 0)
			indexToTextMap->addMappings(startOffset, tokenPositionPairs, tppCount);

		// if we are still missing a "</document!>" tag, add it now
		if (!lastTokenWasCloseDoc)
			ADD_POSTING("</document!>", startOffset + lastSequenceNumber);

		if (tokenBufferPos > 0) {
			indexManager->addPostings(tokenBuffer, tokenBufferPos);
			tokenBufferPos = 0;
		}

		fileManager->changeTokenCount(newFileName, tokenCount, 0);
		fileManager->updateFileAttributes(newFileName);

		updateOperationsPerformed++;
	} // end else [tokenCount > 0]

#if SUPPORT_APPEND_TAQT
	if (AddressSpaceTransformation::getInitialTokenCount(startOffset) <= 0)
		AddressSpaceTransformation::setInitialTokenCount(startOffset, tokenCount);
#endif

	// addFile operation finished; release resources
	if (mustReleaseLock)
		releaseLock();

addFile_END:

	free(newFileName);
	free(tokenPositionPairs);
	delete inputStream;
	return statusCode;
} // end of addFile(char*)


ExtentList * Index::getPostings(const char *term, uid_t userID) {
	return getPostings(term, userID, true, true);
}


void Index::preprocessTerm(char *term) {
	int termLen = strlen(term);
	if ((termLen <= 0) || (termLen > MAX_TOKEN_LENGTH)) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Term with illegal length passed to Index::getPostings(): %s", term);
		log(LOG_ERROR, LOG_ID, errorMessage);
		term[0] = 0;
		return;
	}

	// make sure that special character '$' (stemming) only appears at end of term
	for (int i = 0; i < termLen - 1; i++) {
		if (term[i] == '$') {
			term[0] = 0;
			return;
		}
	}

	if ((STEMMING_LEVEL > 2) && (term[termLen - 1] != '$')) {
		// STEMMING_LEVEL > 2 means we don't keep any non-stemmed postings; therefore,
		// we have to convert everything into stemmed form at query time
		term[termLen++] = '$';
	}

	if (term[termLen - 1] == '$') {
	 	// stem term, if requested by caller or required because of stemming level
		char *t = term;
		if (strncmp(term, "<!>", 3) == 0) {
			t = &term[3];
			termLen -= 3;
		}

		// remove trailing '$'
		t[termLen - 1] = 0;

		char stemmed[MAX_TOKEN_LENGTH * 2];
		strcpy(stemmed, t);
		Stemmer::stem(stemmed, LANGUAGE_ENGLISH, false);

		// if the term is stemmable, replace term string by stemmed form; otherwise,
		// do nothing (but remove the trailing '$', as already done)
		if (stemmed[0] != 0)
			sprintf(t, "%s$", stemmed);
	}
} // end of preprocessTerm(char*)


ExtentList * Index::getPostings(const char *term, uid_t userID, bool fromDisk, bool fromMemory) {
	if (strcasecmp(term, "<file!>") == 0)
		return securityManager->getVisibleExtentStarts(userID);
	if (strcasecmp(term, "</file!>") == 0)
		return securityManager->getVisibleExtentEnds(userID);

	// create copy of "term" and send to preprocessor; the preprocessor will take
	// care of stemming and will transform all illegal input into empty string
	char term2[MAX_TOKEN_LENGTH + 4];
	strncpy(term2, term, sizeof(term2));
	term2[MAX_TOKEN_LENGTH + 2] = 0;
	preprocessTerm(term2);

	// create result list and return to caller
	ExtentList *result;
	if ((indexManager == NULL) || (term2[0] == 0))
		result = new ExtentList_Empty();
	else {
		result = indexManager->getPostings(term2, fromDisk, fromMemory);
		result = Simplifier::simplifyList(result);
#if SUPPORT_APPEND_TAQT
		AddressSpaceTransformation *trafo = AddressSpaceTransformation::getRules();
		result = ExtentList_Transformation::transformList(result, trafo);
#endif
	}
	return result;
} // end of getPostings(char*, uid_t)


void Index::getPostings(char **terms, int termCount, uid_t userID, ExtentList **results) {
	// allocate space for a copy of each term
	char **termCopies = typed_malloc(char*, termCount);

	// traverse the list of terms; give special treatment to special cases
	for (int i = 0; i < termCount; i++) {
		termCopies[i] = NULL;
		results[i] = NULL;
		if (terms[i] == NULL)
			continue;

		int len = strlen(terms[i]);
		termCopies[i] = (char*)malloc(len + 4);
		strcpy(termCopies[i], terms[i]);
		preprocessTerm(termCopies[i]);

		if (termCopies[i][0] == 0)
			results[i] = new ExtentList_Empty();
		else if (strcasecmp(terms[i], "<file!>") == 0)
			results[i] = securityManager->getVisibleExtentStarts(userID);
		else if (strcasecmp(terms[i], "</file!>") == 0)
			results[i] = securityManager->getVisibleExtentEnds(userID);
		if (results[i] != NULL) {
			free(termCopies[i]);
			termCopies[i] = NULL;
		}
	} // end for (int i = 0; i < termCount; i++)

	// fetch all lists from index
	if (indexManager != NULL)
		indexManager->getPostings(termCopies, termCount, true, true, results);

	// free temporary resources, and simplify all result lists if possible
	for (int i = 0; i < termCount; i++) {
		if (termCopies[i] != NULL) {
			free(termCopies[i]);
			if (results[i] != NULL)
				results[i] = Simplifier::simplifyList(results[i]);
			else
				results[i] = new ExtentList_Empty();
		}
	}
	free(termCopies);
} // end of getPostings(char**, int, uid_t, ExtentList**)


void Index::addAnnotation(offset position, const char *annotation) {
	annotator->addAnnotation(position, annotation);
} // end of addAnnotation(offset, char*)


void Index::getAnnotation(offset position, char *buffer) {
	annotator->getAnnotation(position, buffer);
} // end of getAnnotation(offset, char*)


void Index::removeAnnotation(offset position) {
	annotator->removeAnnotation(position);
} // end of removeAnnotation(offset)


offset Index::getBiggestOffset() {
	return fileManager->getBiggestOffset();
} // end of getBiggestOffset()


int Index::getDocumentType(const char *fullPath) {
	IndexedINodeOnDisk iiod;
	if (fileManager->getINodeInfo(fullPath, &iiod))
		return iiod.coreData.documentType;
	else
		return FilteredInputStream::DOCUMENT_TYPE_UNKNOWN;
} // end of getDocumentType(char*)


VisibleExtents * Index::getVisibleExtents(uid_t userID, bool merge) {
	if (APPLY_SECURITY_RESTRICTIONS)
		return securityManager->getVisibleExtents(userID, merge);
	else
		return securityManager->getVisibleExtents(GOD, merge);
} // end of getVisibleExtents(uid_t, bool)


uid_t Index::getOwner() {
	return indexOwner;
} // end of getOwner()


bool Index::directoryAllowed(const char *directoryName) {
	assert(directoryName != NULL);
	if (directoryName[0] == 0)
		return false;

	int len = strlen(directoryName);
	char *dirName = duplicateString(directoryName);

	// first, make sure we do not index one of the special directories
	if ((strncmp(dirName, "/dev/", 5) == 0) || (strcmp(dirName, "/dev") == 0) ||
	    (strncmp(dirName, "/sys/", 5) == 0) || (strcmp(dirName, "/sys") == 0) ||
	    (strncmp(dirName, "/proc/", 6) == 0) || (strcmp(dirName, "/proc") == 0)) {
		free(dirName);
		return false;
	}

	char *fileName = (char*)malloc(len + 20);
	sprintf(fileName, "%s/%s", dirName, ".index_disallow");
	struct stat buf;
	bool result = (stat(fileName, &buf) != 0);
	if (!result)
		goto directoryAllowed_EXIT;

	// remove the last component of the path name
	while (dirName[len - 1] != '/')
		dirName[--len] = 0;
	dirName[--len] = 0;

	sprintf(fileName, "%s/%s", dirName, ".index_disallow");
	result = (stat(fileName, &buf) != 0);

directoryAllowed_EXIT:
	free(fileName);
	free(dirName);
	return result;
} // end of directoryAllowed(char*)


bool Index::getLastIndexToTextSmallerEq(offset where, offset *indexPosition,
			off_t *filePosition) {
	bool mustReleaseLock = getLock();
	bool result = indexToTextMap->getLastSmallerEq(where, indexPosition, filePosition);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getLastIndexToTextSmallerEq(...)


void Index::getDictionarySize(offset *lower, offset *upper) {
	indexManager->getDictionarySize(lower, upper);
} // end of getDictionarySize(offset*, offset*)


int64_t Index::registerForUse() {
	return registerForUse(-1);
}


int64_t Index::registerForUse(int64_t suggestedID) {
	bool mustReleaseLock = getLock();
	registeredUserCount++;
	if (mustReleaseLock)
		releaseLock();
	return indexManager->registerUser(suggestedID);
} // end of registerForUse(int64_t)


void Index::deregister(int64_t id) {
	bool mustReleaseLock = getLock();
	indexManager->deregisterUser(id);
	registeredUserCount--;
	if (mustReleaseLock)
		releaseLock();
} // end of deregister(int64_t)


void Index::waitForUsersToFinish() {
	bool mustReleaseLock = getLock();
	registrationID = -1;
	if (mustReleaseLock)
		releaseLock();
	while (registeredUserCount > 0)
		waitMilliSeconds(INDEX_WAIT_INTERVAL);
} // end of waitForUsersToFinish()


void Index::setMountPoint(const char *mountPoint) {
	if ((fileManager != NULL) && (mountPoint != NULL))
		fileManager->setMountPoint(mountPoint);
} // end of setMountPoint(char*)


void Index::getIndexSummary(char *buffer) {
	if (buffer == NULL)
		return;
	if (indexType == TYPE_INDEX) {
		bool mustReleaseLock = getLock();
		int fc, dc;
		fileManager->getFileAndDirectoryCount(&fc, &dc);
		if (baseDirectory[0] != 0) {
			sprintf(buffer, "%s\t%d %s\t%d %s\n", baseDirectory,
					fc, (fc == 1 ? "file" : "files"), dc, (dc == 1 ? "directory" : "directories"));
		}
		else {
			char *mountPoint = fileManager->getMountPoint();
			sprintf(buffer, "%s\t%d %s\t%d %s\n", mountPoint,
					fc, (fc == 1 ? "file" : "files"), dc, (dc == 1 ? "directory" : "directories"));
			free(mountPoint);
		}
		if (mustReleaseLock)
			releaseLock();
	}
	else
		buffer[0] = 0;
} // end of getIndexSummary(char*)


int64_t Index::getTimeStamp(bool withLocking) {
	if (!withLocking)
		return registrationID;
	else {
		bool mustReleaseLock = getLock();
		int64_t result = registrationID;
		if (mustReleaseLock)
			releaseLock();
		return result;
	}
} // end of getTimeStamp(bool)


bool Index::mayAccessFile(uid_t userID, const char *path) {
	bool mustReleaseLock = getLock();
	bool result = false;
	if (fileManager != NULL) {
		if (APPLY_SECURITY_RESTRICTIONS)
			result = fileManager->mayAccessFile(userID, path);
		else
			result = fileManager->mayAccessFile(GOD, path);
	}
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of mayAccessFile(uid_t, char*)

LanguageModel * Index::getStaticLanguageModel() {
	if (getCache() == NULL)
		return NULL;
	int cacheSize;
	LanguageModel *languageModel = (LanguageModel*)
		getCache()->getPointerToMiscDataFromCache("STATIC_LANGUAGE_MODEL", &cacheSize);
	if (languageModel == NULL) {
		char fileName[MAX_CONFIG_VALUE_LENGTH];
		getConfigurationValue("STATIC_LANGUAGE_MODEL", fileName);
		languageModel = new LanguageModel(fileName);
		if (languageModel->corpusSize <= 1.0) {
			log(LOG_ERROR, LOG_ID,
			"Unable to obtain static language model for intra-document pruning!");
			assert(languageModel->corpusSize > 1.0);
		}
		getCache()->addMiscDataToCache(
				"STATIC_LANGUAGE_MODEL", languageModel, sizeof(LanguageModel), false);
	}
	return languageModel;
} // end of getStaticLanguageModel()


void Index::getClassName(char *target) {
	strcpy(target, "Index");
}


