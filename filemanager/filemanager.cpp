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
 * Implementation of the FileManager class.
 *
 * author: Stefan Buettcher
 * created: 2005-02-18
 * changed: 2009-02-01
 **/


#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "filemanager.h"
#include "directorycontent.h"
#include "../filters/inputstream.h"
#include "../index/index.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"


#define FILE_DATA_FILE "index.files"
#define INODE_DATA_FILE "index.inodes"
#define DIRECTORY_DATA_FILE "index.directories"


// declare FileManager class variables
const char * FileManager::LOG_ID = "FileManager";
const int FileManager::MINIMUM_SLOT_COUNT;
const double FileManager::SLOT_GROWTH_RATE;
const double FileManager::SLOT_REPACK_THRESHOLD;
const off_t FileManager::INODE_FILE_HEADER_SIZE;


// enable this if you want extensive sanity checking after every index update
// operation; this will slow down indexing speed significantly
//#define FILEMANAGER_DEBUG


FileManager::FileManager(Index *owner, const char *workDirectory, bool create) {
	this->owner = owner;
	biggestOffset = -1;
	cachedFileID = -1;
	cachedDirID = -1;
	addressSpaceCovered = 0;
	memset(mountPoint, 0, sizeof(mountPoint));

CONSTRUCTOR_START:

	transactionLog = typed_malloc(AddressSpaceChange, INITIAL_TRANSACTION_LOG_SPACE);
	transactionLogSize = 0;
	transactionLogAllocated = INITIAL_TRANSACTION_LOG_SPACE;

	fileDataFile = evaluateRelativePathName(workDirectory, FILE_DATA_FILE);
	iNodeDataFile = evaluateRelativePathName(workDirectory, INODE_DATA_FILE);
	directoryDataFile = evaluateRelativePathName(workDirectory, DIRECTORY_DATA_FILE);

	if (create) {
		if (owner->readOnly) {
			log(LOG_ERROR, LOG_ID, "Cannot create fresh FileManager instance in read-only mode.");
			exit(1);
		}

		// let's create a fresh FileManager instance within the given directory
		int flags = O_RDWR | O_CREAT | O_TRUNC | O_LARGEFILE;
		mode_t mode = DEFAULT_FILE_PERMISSIONS;
		fileData = open(fileDataFile, flags, mode);
		if (fileData < 0)
			assert("Unable to open " FILE_DATA_FILE == NULL);
		iNodeData = open(iNodeDataFile, flags, mode);
		if (iNodeData < 0)
			assert("Unable to open " INODE_DATA_FILE == NULL);
		directoryData = open(directoryDataFile, flags, mode);
		if (directoryData < 0)
			assert("Unable to open " DIRECTORY_DATA_FILE == NULL);

		// initialize mount point to "/"
		strcpy(mountPoint, "/");

		// initialize internal directory data
		directoryCount = 0;
		directorySlotsAllocated = MINIMUM_SLOT_COUNT;
		directories = typed_malloc(IndexedDirectory, directorySlotsAllocated);
		for (int i = 0; i < directorySlotsAllocated; i++)
			directories[i].id = -1;

		// create root directory (mount point)
		directories[0].id = 0;
		directories[0].parent = 0;
		directories[0].name[0] = 0;
		directories[0].hashValue = 0;
		directoryCount++;
		initializeDirectoryContent(&directories[0].children);
		updateDirectoryAttributes(mountPoint);

		// create file data and write info to disk
		fileCount = 0;
		fileSlotsAllocated = MINIMUM_SLOT_COUNT;
		files = typed_malloc(IndexedFile, fileSlotsAllocated);
		lseek(fileData, (off_t)0, SEEK_SET);
		forced_write(fileData, &fileCount, sizeof(fileCount));
		forced_write(fileData, &fileSlotsAllocated, sizeof(fileSlotsAllocated));
		for (int i = 0; i < fileSlotsAllocated; i++) {
			files[i].iNode = -1;
			IndexedFileOnDisk ifod;
			ifod.iNode = -1;
			writeIFOD(i, &ifod);
		}
		off_t fileDataSize = 2 * sizeof(int32_t) + fileSlotsAllocated * sizeof(IndexedFileOnDisk);
		forced_ftruncate(fileData, fileDataSize);
		freeFileCount = MINIMUM_SLOT_COUNT;

		// create INode data and write info to disk
		iNodeCount = 0;
		iNodeSlotsAllocated = MINIMUM_SLOT_COUNT;
		iNodes = typed_malloc(IndexedINode, iNodeSlotsAllocated);
		lseek(iNodeData, (off_t)0, SEEK_SET);
		forced_write(iNodeData, &iNodeCount, sizeof(iNodeCount));
		forced_write(iNodeData, &iNodeSlotsAllocated, sizeof(iNodeSlotsAllocated));
		forced_write(iNodeData, &biggestOffset, sizeof(biggestOffset));
		for (int i = 0; i < iNodeSlotsAllocated; i++) {
			iNodes[i].hardLinkCount = 0;
			IndexedINodeOnDisk iiod;
			iiod.coreData = iNodes[i];
			writeIIOD(i, &iiod);
		}
		off_t iNodeDataSize =
			INODE_FILE_HEADER_SIZE + iNodeSlotsAllocated * sizeof(IndexedINodeOnDisk);
		forced_ftruncate(iNodeData, iNodeDataSize);
		biggestINodeID = -1;

		// initialize INode hashtable
		for (int i = 0; i < HASHTABLE_SIZE; i++)
			iNodeHashtable[i] = -1;

		// save directory information to disk
		saveToDisk();

	} // end if (create)
	
	if (!create) {
		// load existing FileManager instance from disk
		int flags = (owner->readOnly ? O_RDONLY : O_RDWR) | O_LARGEFILE;
		fileData = open(fileDataFile, flags);
		if (fileData < 0) {
			log(LOG_ERROR, LOG_ID, "Unable to open data file: " FILE_DATA_FILE);
			perror(NULL);
			exit(1);
		}
		iNodeData = open(iNodeDataFile, flags);
		if (iNodeData < 0) {
			log(LOG_ERROR, LOG_ID, "Unable to open inode file: " INODE_DATA_FILE);
			perror(NULL);
			exit(1);
		}
		directoryData = open(directoryDataFile, flags);
		if (directoryData < 0) {
			log(LOG_ERROR, LOG_ID, "Unable to open directory tree file: " DIRECTORY_DATA_FILE);
			perror(NULL);
			exit(1);
		}

		// load directory data from disk
		lseek(directoryData, (off_t)0, SEEK_SET);
		forced_read(directoryData, mountPoint, sizeof(mountPoint));
		forced_read(directoryData, &directoryCount, sizeof(directoryCount));
		forced_read(directoryData, &directorySlotsAllocated, sizeof(directorySlotsAllocated));
		directories = typed_malloc(IndexedDirectory, directorySlotsAllocated);
		forced_read(directoryData, directories, directorySlotsAllocated * sizeof(IndexedDirectory));
		int directoryCountCheck = 0;
		for (int i = 0; i < directorySlotsAllocated; i++)
			if (directories[i].id >= 0) {
				directoryCountCheck++;
				if (directories[i].children.count == 0)
					initializeDirectoryContent(&directories[i].children);
				else {
					int count = directories[i].children.count;
					DC_ChildSlot *children = typed_malloc(DC_ChildSlot, count);
					forced_read(directoryData, children, count * sizeof(DC_ChildSlot));
					initializeDirectoryContentFromChildList(&directories[i].children, children, count);
					free(children);
				}
			}

		// load file data from disk
		lseek(fileData, (off_t)0, SEEK_SET);
		forced_read(fileData, &fileCount, sizeof(fileCount));
		forced_read(fileData, &fileSlotsAllocated, sizeof(fileSlotsAllocated));
		files = typed_malloc(IndexedFile, fileSlotsAllocated);
		int fileCountCheck = 0;
		for (int i = 0; i < fileSlotsAllocated; i++) {
			IndexedFileOnDisk ifod;
			readIFOD(i, &ifod);
			files[i].iNode = ifod.iNode;
			files[i].parent = ifod.parent;
			files[i].hashValue = getHashValue(ifod.fileName);
			if (files[i].iNode >= 0)
				fileCountCheck++;
		}

		// load INode data from disk
		lseek(iNodeData, (off_t)0, SEEK_SET);
		forced_read(iNodeData, &iNodeCount, sizeof(iNodeCount));
		forced_read(iNodeData, &iNodeSlotsAllocated, sizeof(iNodeSlotsAllocated));
		forced_read(iNodeData, &biggestOffset, sizeof(biggestOffset));
		iNodes = typed_malloc(IndexedINode, iNodeSlotsAllocated);
		int iNodeCountCheck = 0;
		for (int i = 0; i < iNodeSlotsAllocated; i++) {
			IndexedINodeOnDisk iiod;
			readIIOD(i, &iiod);
			iNodes[i] = iiod.coreData;
			if (iNodes[i].hardLinkCount > 0) {
				iNodeCountCheck++;
				biggestINodeID = i;
				addressSpaceCovered += iNodes[i].tokenCount;
				if (iiod.coreData.startInIndex + iiod.coreData.tokenCount - 1 > biggestOffset) {
					printf("i = %d: (%lld, %lld) -- biggestOffset = " OFFSET_FORMAT "\n",
							i,
							static_cast<long long>(iiod.coreData.startInIndex),
							static_cast<long long>(iiod.coreData.tokenCount),
							biggestOffset);
				}
				assert(iiod.coreData.startInIndex + iiod.coreData.tokenCount - 1 <= biggestOffset);
			}
		}

		// initialize INode hashtable
		for (int i = 0; i < HASHTABLE_SIZE; i++)
			iNodeHashtable[i] = -1;
		for (int i = 0; i < iNodeSlotsAllocated; i++)
			if (iNodes[i].hardLinkCount > 0) {
				int32_t hashValue = getINodeHashValue(iNodes[i].iNodeID);
				int32_t hashSlot = hashValue % HASHTABLE_SIZE;
				iNodes[i].nextINode = iNodeHashtable[hashSlot];
				iNodeHashtable[hashSlot] = i;
			}

		if ((directoryCountCheck != directoryCount) || (fileCountCheck != fileCount) ||
		    (iNodeCountCheck != iNodeCount)) {
			char message[256];
			log(LOG_ERROR, LOG_ID, "FileManager found corrupted data. Creating new data structure.");
			sprintf(message, "  directoryCount = %d -- directoryCountCheck = %d\n",
					directoryCount, directoryCountCheck);
			log(LOG_ERROR, LOG_ID, message);
			sprintf(message, "  fileCount = %d -- fileCountCheck = %d\n", fileCount, fileCountCheck);
			log(LOG_ERROR, LOG_ID, message);
			sprintf(message, "  iNodeCount = %d -- iNodeCountCheck = %d\n", iNodeCount, iNodeCountCheck);
			log(LOG_ERROR, LOG_ID, message);

			freeDirectoryIDs = NULL;
			freeFileIDs = NULL;
			freeMemory();
			create = true;
			goto CONSTRUCTOR_START;
		}

	} // end if (!create)

	// initialize free slot information
	freeDirectoryCount = 0;
	freeDirectoryIDs = typed_malloc(int32_t, 1);
	freeFileCount = 0;
	freeFileIDs = typed_malloc(int32_t, 1);

} // end of FileManager(char*, bool)


void FileManager::freeMemory() {
	close(directoryData);
	close(fileData);
	close(iNodeData);

	// free all memory occupied by DirectoryContent objects
	for (int i = 0; i < directorySlotsAllocated; i++)
		if (directories[i].id >= 0)
			freeDirectoryContent(&directories[i].children);

	// release free space arrays
	if (freeDirectoryIDs != NULL)
		free(freeDirectoryIDs);
	if (freeFileIDs != NULL)
		free(freeFileIDs);

	// free all other data
	free(directories);
	free(files);
	free(iNodes);
	
	// release string resources allocated for file names
	free(fileDataFile);
	free(iNodeDataFile);
	free(directoryDataFile);

	// free memory allocated for transaction log
	free(transactionLog);
	transactionLog = NULL;
} // end of freeMemory()


FileManager::~FileManager() {
	if (!owner->readOnly)
		saveToDisk();
	freeMemory();
} // end of ~FileManager()


#ifdef FILEMANAGER_DEBUG

void FileManager::sanityCheck() {
	LocalLock lock(this);

	int directoryCountCheck = 0;
	for (int i = 0; i < directorySlotsAllocated; i++)
		if (directories[i].id >= 0)
			directoryCountCheck++;
	assert(directoryCount == directoryCountCheck);

	int fileCountCheck = 0;
	for (int i = 0; i < fileSlotsAllocated; i++) {
		IndexedFileOnDisk ifod;
		readIFOD(i, &ifod);
		if (ifod.iNode >= 0)
			fileCountCheck++;
	}
	assert(fileCount == fileCountCheck);

	int iNodeCountCheck = 0;
	offset addressSpace = 0;
	for (int i = 0; i < iNodeSlotsAllocated; i++) {
		IndexedINodeOnDisk iiod;
		readIIOD(i, &iiod);
		if (iiod.coreData.hardLinkCount > 0) {
			iNodeCountCheck++;
			addressSpace += iiod.coreData.tokenCount;
		}
	}
	assert(iNodeCount == iNodeCountCheck);
	assert(addressSpace == addressSpaceCovered);
} // end of sanityCheck()

#else

void FileManager::sanityCheck() {
	return;
}

#endif


void FileManager::saveToDisk() {
	LocalLock lock(this);

	// write INode count to disk
	lseek(iNodeData, (off_t)0, SEEK_SET);
	forced_write(iNodeData, &iNodeCount, sizeof(iNodeCount));
	forced_write(iNodeData, &iNodeSlotsAllocated, sizeof(iNodeSlotsAllocated));
	forced_write(iNodeData, &biggestOffset, sizeof(biggestOffset));
	fsync(iNodeData);
	close(iNodeData);
	iNodeData = open(iNodeDataFile, O_RDWR | O_LARGEFILE);

	// write file count to disk
	lseek(fileData, (off_t)0, SEEK_SET);
	forced_write(fileData, &fileCount, sizeof(fileCount));
	forced_write(fileData, &fileSlotsAllocated, sizeof(fileSlotsAllocated));
	fsync(fileData);
	close(fileData);
	fileData = open(fileDataFile, O_RDWR | O_LARGEFILE);

	// write directory data to disk
	lseek(directoryData, (off_t)0, SEEK_SET);
	forced_write(directoryData, mountPoint, sizeof(mountPoint));
	forced_write(directoryData, &directoryCount, sizeof(directoryCount));
	forced_write(directoryData, &directorySlotsAllocated, sizeof(directorySlotsAllocated));
	forced_write(directoryData, directories, directorySlotsAllocated * sizeof(IndexedDirectory));

	// for every directory, save the list of children to disk
	for (int i = 0; i < directorySlotsAllocated; i++) {
		if (directories[i].id == i) {
			mergeLists(&directories[i].children);
			int count = directories[i].children.count;
			if (count > 0)
				forced_write(directoryData, directories[i].children.longList, count * sizeof(DC_ChildSlot));
		}
	} // end for (int i = 0; i < directorySlotsAllocated; i++)
	
	// truncate the data file
	forced_ftruncate(directoryData, lseek(directoryData, (off_t)0, SEEK_CUR));
	fsync(directoryData);
	close(directoryData);
	directoryData = open(directoryDataFile, O_RDWR | O_LARGEFILE);

	// file and INode data do not have to be written to disk because we
	// always keep the on-disk stuff consistent with the in-memory stuff
	if (true);
} // end of saveToDisk()


void FileManager::beginTransaction() {
	getLock();
	assert(transactionLogSize == 0);
	sanityCheck();
} // end of beginTransaction()


void FileManager::finishTransaction() {
	sanityCheck();
	AddressSpaceChange *taLog = NULL;
	int taLogSize = transactionLogSize;
	if (transactionLogSize > 0) {
		taLog = transactionLog;
		transactionLog = typed_malloc(AddressSpaceChange, INITIAL_TRANSACTION_LOG_SPACE);
		transactionLogSize = 0;
		transactionLogAllocated = INITIAL_TRANSACTION_LOG_SPACE;
	}
	releaseLock();
	if (taLog != NULL) {
		for (int i = 0; i < taLogSize; i++)
			owner->notifyOfAddressSpaceChange(taLog[i].delta, taLog[i].startOffset,
					(taLog[i].startOffset + taLog[i].tokenCount) - 1);
		free(taLog);
	}
} // end of finishTransaction()


void FileManager::addToTransactionLog(offset startOffset, uint32_t tokenCount, int32_t delta) {
	if (transactionLogSize >= transactionLogAllocated) {
		transactionLogAllocated *= 2;
		transactionLog = typed_realloc(AddressSpaceChange, transactionLog, transactionLogAllocated);
	}
	AddressSpaceChange *asc = &transactionLog[transactionLogSize];
	asc->startOffset = startOffset;
	asc->tokenCount = tokenCount;
	asc->delta = delta;
	transactionLogSize++;
} // end of addToTransactionLog(offset, uint32_t, int32_t)


offset FileManager::addFile(char *fullPath, int16_t documentType, int16_t language) {
	beginTransaction();

	offset result = -1;
	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		int fileID = getFileID(relPath, true);
		if (fileID >= 0) {
			updateFileAttributes(relPath, fileID);
			int iNode = files[fileID].iNode;
			assert(iNode >= 0);
			IndexedINode *ii = &iNodes[iNode];
			assert(ii->hardLinkCount > 0);
			ii->documentType = documentType;
			ii->language = language;
			ii->tokenCount = 1;
			addToTransactionLog(ii->startInIndex, ii->tokenCount, +1);
			IndexedINodeOnDisk iiod;
			readIIOD(iNode, &iiod);
			iiod.coreData = iNodes[iNode];
			iiod.reservedTokenCount = 1;
			writeIIOD(iNode, &iiod);
			result = iNodes[iNode].startInIndex;
		}
		free(relPath);
	} // end if (relPath != NULL)

	finishTransaction();
	return result;
} // end of addFile(char*, int16_t, int16_t)


void FileManager::changeTokenCount(char *fullPath, uint32_t tokenCount, uint32_t reservedTokenCount) {
	beginTransaction();

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		// obtain inode descriptor for the given file
		int fileID = getFileID(relPath, false);
		assert(fileID >= 0);
		int iNode = files[fileID].iNode;
		assert(iNode >= 0);
		IndexedINode *ii = &iNodes[iNode];
		assert((reservedTokenCount == 0) || (iNode == biggestINodeID));
		assert(ii->hardLinkCount >= 1);
		assert(tokenCount >= ii->tokenCount);

		// fetch inode data from disk to get value of iiod.reservedTokenCount
		IndexedINodeOnDisk iiod;
		readIIOD(iNode, &iiod);

		if (reservedTokenCount == 0)
			reservedTokenCount = iiod.reservedTokenCount;
		if (iNode == biggestINodeID)
			reservedTokenCount = MAX(tokenCount, reservedTokenCount);
		assert(tokenCount <= reservedTokenCount);
		
		if (tokenCount > ii->tokenCount)
			addToTransactionLog(ii->startInIndex + ii->tokenCount, tokenCount - ii->tokenCount, +1);
		else if (tokenCount < iNodes[iNode].tokenCount)
			addToTransactionLog(ii->startInIndex + tokenCount, ii->tokenCount - tokenCount, -1);
		ii->tokenCount = tokenCount;
		iiod.reservedTokenCount = reservedTokenCount;

		// update total size of address space, so that the next file is put into
		// the right spot
		if (biggestOffset < (ii->startInIndex + reservedTokenCount) - 1)
			biggestOffset = (ii->startInIndex + reservedTokenCount) - 1;

		// write inode data back to disk
		iiod.coreData = *ii;
		iiod.timeStamp = time(NULL);
		writeIIOD(iNode, &iiod);

		updateFileAttributes(relPath, fileID);
		free(relPath);
	} // end if (relPath != NULL)

	finishTransaction();
} // end of changeTokenCount(char *fullPath, uint32_t tokenCount)


bool FileManager::removeFile(char *fullPath) {
	bool result = false;
	beginTransaction();

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		int32_t fileID = getFileID(relPath, false);
		if (fileID >= 0) {
			removeFile(fileID);
			result = true;
		}
		free(relPath);
	}

	finishTransaction();
	return result;
} // end of removeFile(char*)


bool FileManager::removeDirectory(char *fullPath) {
	bool result = false;
	beginTransaction();

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		int32_t dirID = getDirectoryID(relPath, false);
		if (dirID >= 0) {
			if (directories[dirID].children.count == 0)
				removeDirectory(dirID);
			else
				removeNonEmptyDirectory(dirID);
			result = true;
		}
		free(relPath);
	} // end if (relPath != NULL)

	finishTransaction();
	return result;
} // end of removeDirectory(char*)


void FileManager::updateFileAttributes(char *fullPath) {
	beginTransaction();

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		updateFileAttributes(relPath, -1);
		free(relPath);
	}
		
	finishTransaction();
} // end of updateFileAttributes(char*)


void FileManager::updateDirectoryAttributes(char *fullPath) {
	struct stat buf;
	beginTransaction();

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		int id = getDirectoryID(relPath, false);
		if (id >= 0) {
			if (stat(fullPath, &buf) == 0) {
				directories[id].owner = buf.st_uid;
				directories[id].group = buf.st_gid;
				directories[id].permissions = buf.st_mode;
			}
		}
		free(relPath);
	} // end if (relPath != NULL)

	finishTransaction();
} // end of updateDirectoryAttributes(char*)


bool FileManager::renameFileOrDirectory(char *oldPath, char *newPath) {
	struct stat buf;
	if (stat(newPath, &buf) != 0)
		return false;
	beginTransaction();

	oldPath = makeRelativeToMountPoint(oldPath);
	if (oldPath == NULL) {
		finishTransaction();
		return false;
	}
	newPath = makeRelativeToMountPoint(newPath);
	if (newPath == NULL) {
		char *fullPath = evaluateRelativePathName(mountPoint, oldPath);
		finishTransaction();
		removeFile(fullPath);
		free(oldPath);
		free(fullPath);
	}

	// split the new path name into directory and last part
	char *lastPart = &newPath[strlen(newPath) - 1];
	while (*lastPart != '/')
		lastPart--;
	*(lastPart++) = 0;

	bool result = true;

	if (S_ISDIR(buf.st_mode)) {
		// thing is a directory
		int id = getDirectoryID(oldPath, false);
		if (id >= 0) {
			removeDirectoryFromDirectory(id, directories[id].parent);
			strcpy(directories[id].name, lastPart);
			directories[id].hashValue = getHashValue(lastPart);
			int newParent = getDirectoryID(newPath, true);
			if (newParent >= 0)
				addDirectoryToDirectory(id, newParent);
			else
				removeDirectory(id);
		}
		else
			result = false;
	}
	else if (S_ISREG(buf.st_mode)) {
		// thing is an ordinary file
		int id = getFileID(oldPath, false);
		if (id >= 0) {
			removeFileFromDirectory(id, files[id].parent);
			IndexedFileOnDisk ifod;
			readIFOD(id, &ifod);
			strcpy(ifod.fileName, lastPart);
			writeIFOD(id, &ifod);
			files[id].hashValue = getHashValue(lastPart);
			int newParent = getDirectoryID(newPath, true);
			if (newParent >= 0) {
				files[id].parent = ifod.parent = newParent;
				writeIFOD(id, &ifod);
				addFileToDirectory(id, newParent);
			}
			else {
				files[id].parent = ifod.parent = -1;
				writeIFOD(id, &ifod);
				removeFile(id);
			}
		}
		else
			result = false;
	}
	else {
		// thing is neither directory nor file: ignore!
		result = false;
	}

	free(oldPath);
	free(newPath);
	finishTransaction();
	return result;
} // end of renameFileOrDirectory(char*, char*)


void FileManager::removeAllInexistentFiles() {
	log(LOG_DEBUG, LOG_ID, "removeAllInexistentFiles(): Started.");

	for (int i = 0; i < fileSlotsAllocated; i++) {
		beginTransaction();
		if (i < fileSlotsAllocated)
			if (files[i].iNode >= 0) {
				char *path = getFilePath(i);
				if (*path != 0) {
					struct stat buf;
					if (stat(path, &buf) != 0)
						removeFile(i);
					else if (!S_ISREG(buf.st_mode))
						removeFile(i);
				}
				free(path);
			}
		finishTransaction();
	} // end for (int i = 0; i < fileSlotsAllocated; i++)

	log(LOG_DEBUG, LOG_ID, "removeAllInexistentFiles(): Done.");
} // end of removeAllInexistentFiles()


bool FileManager::updateFileAttributes(char *relPath, int fileID) {
	bool mustReleaseLock = getLock();

	// determine fileID, if not provided by caller
	if (fileID < 0) {
		fileID = getFileID(relPath, false);
		if (fileID < 0) {
			if (mustReleaseLock)
				releaseLock();
			return false;
		}
	}

	struct stat buf;
	ino_t iNodeID;
	char *fullPath = evaluateRelativePathName(mountPoint, relPath);
	if (stat(fullPath, &buf) == 0)
		iNodeID = buf.st_ino;
	else
		iNodeID = (ino_t)(-1);
	free(fullPath);

	int oldINode = files[fileID].iNode;

	// check if current INode ID of file is consistent with old INode data;
	// if not, remove hard link
	if (oldINode >= 0)
		if (iNodes[oldINode].iNodeID != iNodeID) {
			if (iNodes[oldINode].hardLinkCount <= 1)
				releaseINodeID(oldINode);
			else {
				iNodes[oldINode].hardLinkCount--;
				updateINodeOnDisk(oldINode);
			}
			oldINode = files[fileID].iNode = -1;
		}

	// if this file object does not correspond to a real file (in the file system),
	// free the resources occupied by the file
	if (iNodeID < 0) {
		if (files[fileID].parent >= 0)
			removeFileFromDirectory(fileID, files[fileID].parent);
		if (mustReleaseLock)
			releaseLock();
		return false;
	}

	// if the file currently does not belong to any INode, obtain new INode
	// ID and add hard link to that INode
	if (oldINode < 0) {
		oldINode = obtainINodeID();
		files[fileID].iNode = oldINode;
		iNodes[oldINode].hardLinkCount = 1;
		iNodeCount++;
		updateINodeOnDisk(oldINode);
		IndexedFileOnDisk ifod;
		readIFOD(fileID, &ifod);
		ifod.iNode = oldINode;
		writeIFOD(fileID, &ifod);
	}
	int iNode = oldINode;

	// update INode attributes
	iNodes[iNode].iNodeID = iNodeID;
	iNodes[iNode].owner = buf.st_uid;
	iNodes[iNode].group = buf.st_gid;
	iNodes[iNode].permissions = buf.st_mode;
	IndexedINodeOnDisk iiod;
	readIIOD(iNode, &iiod);
	iiod.coreData = iNodes[iNode];
	iiod.timeStamp = time(NULL);
	iiod.fileSize = buf.st_size;
	writeIIOD(iNode, &iiod);

	if (mustReleaseLock)
		releaseLock();

	return true;
} // end of updateFileAttributes(char*, int)


void FileManager::removeFile(int id) {
	bool mustReleaseLock = getLock();
	if (files[id].parent >= 0)
		removeFileFromDirectory(id, files[id].parent);
	int iNode = files[id].iNode;
	releaseFileID(id);
	if (iNode < 0)
		goto removeFile_EXIT;
	if (iNodes[iNode].hardLinkCount <= 1)
		releaseINodeID(iNode);
	else {
		iNodes[iNode].hardLinkCount--;
		IndexedINodeOnDisk iiod;
		readIIOD(iNode, &iiod);
		iiod.coreData = iNodes[iNode];
		iiod.timeStamp = time(NULL);
		writeIIOD(iNode, &iiod);
	}
removeFile_EXIT:
	if (mustReleaseLock)
		releaseLock();
} // end of removeFile(int)


bool FileManager::setMountPoint(const char *newMountPoint) {
	if ((newMountPoint[0] == 0) || (strlen(newMountPoint) >= sizeof(mountPoint) - 2))
		return false;
	LocalLock lock(this);

	strcpy(mountPoint, newMountPoint);
	if (mountPoint[strlen(mountPoint) - 1] != '/')
		strcat(mountPoint, "/");
	updateDirectoryAttributes(mountPoint);
// XXX disabled for now XXX
//	removeAllInexistentFiles();
	return true;
} // end of setMountPoint(char*)


char * FileManager::getMountPoint() {
	bool mustReleaseLock = getLock();
	char *result = duplicateString(mountPoint);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getMountPoint()


int32_t FileManager::getINodeHashValue(ino_t fsID) {
	if (fsID < 0)
		fsID = -(fsID + 1);
	int32_t hashValue = fsID % 2000000011;
	return hashValue;
} // end of getINodeHashValue(ino_t)


bool FileManager::mayAccessFile(uint16_t permissions, uid_t fileOwner,
		gid_t fileGroup, uid_t userID, gid_t *groups, int groupCount) {
	if (fileOwner == userID)
		return ((permissions & S_IRUSR) != 0);
	else if (userIsInGroup(fileGroup, groups, groupCount))
		return ((permissions & S_IRGRP) != 0);
	else
		return ((permissions & S_IROTH) != 0);
} // end of mayAccessFile(uint16_t, uid_t, gid_t, uid_t, gid_t*, int)


bool FileManager::mayAccessDirectory(uint16_t permissions, uid_t fileOwner,
		gid_t fileGroup, uid_t userID, gid_t *groups, int groupCount) {
	if (fileOwner == userID)
		return ((permissions & S_IXUSR) != 0);
	else if (userIsInGroup(fileGroup, groups, groupCount))
		return ((permissions & S_IXGRP) != 0);
	else
		return ((permissions & S_IXOTH) != 0);
} // end of mayAccessDirectory(uint16_t, uid_t, gid_t, uid_t, gid_t*, int)
		

bool FileManager::mayAccessFile(uid_t userID, const char *fullPath) {
	bool mustReleaseLock = getLock();
	int groupCount, dir, iNode;
	int fileID = -1;
	gid_t *groups = computeGroupsForUser(userID, &groupCount);

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		fileID = getFileID(relPath, false);
		free(relPath);
	}
	if (fileID < 0)
		goto mayAccessFile_EXIT;
	if ((userID == Index::GOD) || (userID == Index::SUPERUSER))
		goto mayAccessFile_EXIT;

	iNode = files[fileID].iNode;
	if (iNode < 0) {
		fileID = -1;
		goto mayAccessFile_EXIT;
	}
	if (!mayAccessFile(iNodes[iNode].permissions, iNodes[iNode].owner,
				iNodes[iNode].group, userID, groups, groupCount)) {
		fileID = -1;
		goto mayAccessFile_EXIT;
	}

	dir = files[fileID].parent;
	while (dir != 0) {
		if (!mayAccessDirectory(directories[dir].permissions, directories[dir].owner,
					directories[dir].group, userID, groups, groupCount)) {
			fileID = -1;
			goto mayAccessFile_EXIT;
		}
		dir = directories[dir].parent;
	} // end while (dir != 0)

mayAccessFile_EXIT:

	free(groups);
	if (mustReleaseLock)
		releaseLock();
	return (fileID >= 0);
} // end of mayAccessFile(uid_t, char*)


bool FileManager::changedSinceLastUpdate(char *fullPath) {
	LocalLock lock(this);
	int iNode = -1, fileID = -1;
	IndexedINodeOnDisk iiod;
	struct stat buf;

	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		fileID = getFileID(relPath, false);
		free(relPath);
	}
	if (fileID < 0)
		return true;
	iNode = files[fileID].iNode;
	if (iNode < 0)
		return true;
	readIIOD(iNode, &iiod);
	if (stat(fullPath, &buf) != 0)
		return true;
	if (buf.st_mtime > iiod.timeStamp)
		return true;
	if (buf.st_size != iiod.fileSize)
		return true;

	return false;
} // end of changedSinceLastUpdate(char*)


int FileManager::getINode(ino_t fsID) {
	int hashValue = getINodeHashValue(fsID);
	int hashSlot = hashValue % HASHTABLE_SIZE;
	int id = iNodeHashtable[hashSlot];
	while (id >= 0) {
		if (iNodes[id].iNodeID == fsID)
			return id;
		id = iNodes[id].nextINode;
	}
	return -1;
} // end of getINode(ino_t)


char * FileManager::toCanonicalForm(const char *path) {
	char *collapsed = duplicateString(path);
	collapsePath(collapsed);
	// check if all components have acceptable length
	StringTokenizer *tok = new StringTokenizer(collapsed, "/");
	while (tok->hasNext()) {
		char *component = tok->getNext();
		if ((tok->hasNext()) && (strlen(component) > MAX_DIRECTORY_NAME_LENGTH)) {
			delete tok;
			free(collapsed);
			return NULL;
		}
		if ((!tok->hasNext()) && (strlen(component) > MAX_FILE_NAME_LENGTH)) {
			delete tok;
			free(collapsed);
			return NULL;
		}
	}
	delete tok;
	return collapsed;
} // end of toCanonicalForm(char*)


char * FileManager::makeRelativeToMountPoint(const char *fullPath) {
	if ((fullPath == NULL) || (fullPath[0] == 0))
		return NULL;
	char *fp = toCanonicalForm(fullPath);
	if (startsWith(fp, mountPoint)) {
		char *result = duplicateString(&fp[strlen(mountPoint)]);
		free(fp);
		if (result[0] == '/')
			return result;
		else
			return concatenateStringsAndFree(duplicateString("/"), result);
	}
	else if ((startsWith(mountPoint, fp)) && (strlen(mountPoint) == strlen(fp) + 1)) {
		strcpy(fp, "/");
		return fp;
	}
	else
		return fp;
} // end of makeRelativeToMountPoint(char*)


int32_t FileManager::getHashValue(char *s) {
	int32_t result = 0;
	while (s[0] != 0) {
		result = (result % 8388013) * 256 + (byte)(s[0]);
		s++;
	}
	if (result >= 0)
		return result;
	else
		return -result;
} // end of getHashValue(char*)


void FileManager::removeDirectoryFromDirectory(int id, int parent) {
	assert(parent == directories[id].parent);
	removeDirectoryFromDC(id, &directories[parent].children, directories);
	directories[id].parent = -1;
	if (directories[parent].children.count == 0)
		removeDirectory(parent);
} // end of removeDirectoryFromDirectory()


void FileManager::removeFileFromDirectory(int id, int parent) {
	assert(parent == files[id].parent);
	removeFileFromDC(id, &directories[parent].children, files);
	files[id].parent = -1;
	if (directories[parent].children.count == 0)
		removeDirectory(parent);
} // end of removeFileFromDirectory()


int32_t FileManager::getDirectoryID(int32_t dir, char *name, bool createOnDemand) {
	int32_t result = findDirectoryInDC(name, &directories[dir].children, directories);
	if (result >= 0)
		return result;
	else if (!createOnDemand)
		return -1;
	result = obtainDirectoryID();
	directories[result].id = result;
	directories[result].parent = dir;
	strcpy(directories[result].name, name);
	directories[result].hashValue = getHashValue(name);
	addDirectoryToDC(result, &directories[dir].children, directories);
	char *fullPath = getDirectoryPath(result);
	updateDirectoryAttributes(fullPath);
	free(fullPath);
	return result;
} // end of getDirectoryID(int32_t, char*, bool)


void FileManager::addDirectoryToDirectory(int id, int parent) {
	addDirectoryToDC(id, &directories[parent].children, directories);
	directories[id].parent = parent;
} // end of addDirectoryToDirectory(int, int)


void FileManager::addFileToDirectory(int id, int parent) {
	addFileToDC(id, &directories[parent].children, files);
	files[id].parent = parent;
} // end of addFileToDirectory(int, int)


int32_t FileManager::getFileID(int32_t dir, char *name, bool createOnDemand) {
	int32_t result = findFileInDC(name, &directories[dir].children, this);
	if (result >= 0)
		return result;
	else if (!createOnDemand)
		return -1;
	result = obtainFileID();
	IndexedFileOnDisk ifod;
	memset(&ifod, 0, sizeof(ifod));
	ifod.iNode = -1;
	ifod.parent = dir;
	strcpy(ifod.fileName, name);
	writeIFOD(result, &ifod);
	files[result].parent = dir;
	files[result].hashValue = getHashValue(name);
	files[result].iNode = -1;
	addFileToDC(result, &directories[dir].children, files);
	return result;
} // end of getFileID(int32_t, char*, bool)


int32_t FileManager::getFileID(char *relPath, bool createOnDemand) {
	// transform to canonical form and check if string is valid
	char *path = toCanonicalForm(relPath);
	if (path == NULL)
		return -1;

	if (cachedFileID >= 0)
		if (strcmp(path, cachedFileName) == 0) {
			free(path);
			return cachedFileID;
		}

	// path name is OK; insert file into directory structure
	int currentDirectory = 0;
	StringTokenizer *tok = new StringTokenizer(path, "/");
	while (tok->hasNext()) {
		char *component = tok->getNext();
		if (*component == 0)
			continue;
		if (tok->hasNext()) {
			// not arrived at end of string yet: this component is a directory.
			// get ID of new directory, create descriptor on demand.
			currentDirectory = getDirectoryID(currentDirectory, component, createOnDemand);
			if (currentDirectory < 0) {
				delete tok;
				free(path);
				return -1;
			}
		}
		else {
			// arrived at end of string: this component is the file name.
			int32_t result = getFileID(currentDirectory, component, createOnDemand);
			delete tok;
			if (strlen(path) < 255) {
				cachedFileID = result;
				strcpy(cachedFileName, path);
			}
			free(path);
			return result;
		}
	} // end while (tok->hasNext())

	// getting here means that the original string did not contain any tokens
	// (i.e., string was "/"); this this case, we have to return -1
	delete tok;
	free(path);
	return -1;
} // end of getFileID(char*, bool)


int32_t FileManager::getDirectoryID(char *relPath, bool createOnDemand) {
	// transform to canonical form and check if string is valid
	char *path = toCanonicalForm(relPath);
	if (path == NULL)
		return -1;

	if (cachedDirID >= 0)
		if (strcmp(path, cachedDirName) == 0) {
			free(path);
			return cachedDirID;
		}

	// path name is OK; insert dir into directory structure
	int currentDirectory = 0;
	StringTokenizer *tok = new StringTokenizer(path, "/");
	while (tok->hasNext()) {
		char *component = tok->getNext();
		if (*component == 0)
			continue;
		if (tok->hasNext()) {
			// not arrived at end of string yet: this component is a directory.
			// get ID of new directory, create descriptor on demand.
			currentDirectory = getDirectoryID(currentDirectory, component, createOnDemand);
			if (currentDirectory < 0) {
				delete tok;
				free(path);
				return -1;
			}
		}
		else {
			// arrived at end of string: this component is the file name.
			int32_t result = getDirectoryID(currentDirectory, component, createOnDemand);
			delete tok;
			if (strlen(path) < 256) {
				cachedDirID = result;
				strcpy(cachedDirName, path);
			}
			free(path);
			return result;
		}
	} // end while (tok->hasNext())

	delete tok;
	free(path);
	return currentDirectory;
} // end of getDirectoryID(char*, bool)


bool FileManager::readIFOD(int32_t fileID, IndexedFileOnDisk *ifod) {
	bool mustReleaseLock = getLock();
	bool result = false;
	if ((fileID >= 0) && (fileID < fileSlotsAllocated)) {
		off_t headerLength = 2 * sizeof(int32_t);
		lseek(fileData, headerLength + fileID * sizeof(IndexedFileOnDisk), SEEK_SET);
		int gelesen = forced_read(fileData, ifod, sizeof(IndexedFileOnDisk));
		assert(gelesen == sizeof(IndexedFileOnDisk));
		result = true;
	}
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of readIFOD(int, IndexedFileOnDisk*)


void FileManager::writeIFOD(int fileID, IndexedFileOnDisk *ifod) {
	off_t headerLength = 2 * sizeof(int32_t);
	lseek(fileData, headerLength + fileID * sizeof(IndexedFileOnDisk), SEEK_SET);
	int result = forced_write(fileData, ifod, sizeof(IndexedFileOnDisk));
	assert(result == sizeof(IndexedFileOnDisk));
} // end of writeIFOD(int, IndexedFileOnDisk*)


void FileManager::readIIOD(int id, IndexedINodeOnDisk *iiod) {
	lseek(iNodeData, INODE_FILE_HEADER_SIZE + id * sizeof(IndexedINodeOnDisk), SEEK_SET);
	int result = forced_read(iNodeData, iiod, sizeof(IndexedINodeOnDisk));
	assert(result == sizeof(IndexedINodeOnDisk));
} // end of readIIOD(int, IndexedINodeOnDisk*)


void FileManager::writeIIOD(int id, IndexedINodeOnDisk *iiod) {
	lseek(iNodeData, INODE_FILE_HEADER_SIZE + id * sizeof(IndexedINodeOnDisk), SEEK_SET);
	int result = forced_write(iNodeData, iiod, sizeof(IndexedINodeOnDisk));
	assert(result == sizeof(IndexedINodeOnDisk));
} // end of writeIIOD(int, IndexedINodeOnDisk*)


void FileManager::removeDirectory(int directoryID) {
	if (directoryID == 0)
		return;
	int parent = directories[directoryID].parent;
	if (parent >= 0) {
		removeDirectoryFromDirectory(directoryID, parent);
		directories[directoryID].parent = -1;
	}
	releaseDirectoryID(directoryID);
} // end of removeDirectory(int)


void FileManager::removeNonEmptyDirectory(int directoryID) {
	if (directoryID == 0)
		return;
	mergeLists(&directories[directoryID].children);
	for (int i = 0; i < directories[directoryID].children.count; i++) {
		int id = directories[directoryID].children.longList[i].id;
		if (id < 0) {
			// handle directory case
			directories[-id].parent = -1;
			removeNonEmptyDirectory(-id);
		}
		else {
			// handle file case
			files[id].parent = -1;
			removeFile(id);
		}
	}
	removeDirectory(directoryID);
} // end of removeNonEmptyDirectory(int)


char * FileManager::getDirectoryPath(int32_t id) {
	bool mustReleaseLock = getLock();

	if ((id < 0) || (id >= directorySlotsAllocated)) {
		if (mustReleaseLock)
			releaseLock();
		return duplicateString("");
	}

	char *result = duplicateString(directories[id].name);
	id = directories[id].parent;
	while (id != 0) {
		char *temp = evaluateRelativePathName(directories[id].name, result);
		free(result);
		result = temp;
		id = directories[id].parent;
	}
	char *temp = evaluateRelativePathName(mountPoint, result);
	free(result);
	result = temp;

	if (mustReleaseLock)
		releaseLock();

	return result;
} // end of getDirectoryPath(int32_t)


char * FileManager::getFilePath(int32_t id) {
	bool mustReleaseLock = getLock();

	if ((id < 0) || (id >= fileSlotsAllocated)) {
		if (mustReleaseLock)
			releaseLock();
		return duplicateString("");
	}
	if (files[id].iNode < 0) {
		if (mustReleaseLock)
			releaseLock();
		return duplicateString("");
	}

	IndexedFileOnDisk ifod;
	readIFOD(id, &ifod);
	char *result = duplicateString(ifod.fileName);
	id = ifod.parent;
	while (id != 0) {
		char *temp = evaluateRelativePathName(directories[id].name, result);
		free(result);
		result = temp;
		id = directories[id].parent;
	}
	char *temp = evaluateRelativePathName(mountPoint, result);
	free(result);
	result = temp;

	if (mustReleaseLock)
		releaseLock();

	return result;
} // end of getFilePath(int32_t)


bool FileManager::getINodeInfo(int32_t fileID, IndexedINodeOnDisk *iiod) {
	bool mustReleaseLock = getLock();
	bool result = false;
	if ((fileID >= 0) && (fileID < fileSlotsAllocated)) {
		readIIOD(files[fileID].iNode, iiod);
		result = (iiod->coreData.hardLinkCount > 0);
	}
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getINodeInfo(int32_t, IndexedINodeOnDisk*)


bool FileManager::getINodeInfo(const char *fullPath, IndexedINodeOnDisk *iiod) {
	bool mustReleaseLock = getLock();
	bool result = false;
	char *relPath = makeRelativeToMountPoint(fullPath);
	if (relPath != NULL) {
		int fileID = getFileID(relPath, false);
		if (fileID >= 0)
			result = getINodeInfo(fileID, iiod);
		free(relPath);
	}
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getINodeInfo(char*, IndexedINodeOnDisk*)


offset FileManager::getBiggestOffset() {
	return biggestOffset;
} // end of getBiggestOffset()


int32_t FileManager::obtainDirectoryID() {
	if (freeDirectoryCount == 0) {
		freeDirectoryCount = directorySlotsAllocated - directoryCount;
		if ((freeDirectoryCount < 0.1 * directorySlotsAllocated) ||
		    (freeDirectoryCount < MINIMUM_SLOT_COUNT)) {
			int newCount = (int)(directorySlotsAllocated * SLOT_GROWTH_RATE);
			if (newCount < directorySlotsAllocated + MINIMUM_SLOT_COUNT)
				newCount = directorySlotsAllocated + MINIMUM_SLOT_COUNT;
			typed_realloc(IndexedDirectory, directories, newCount);
			for (int i = directorySlotsAllocated; i < newCount; i++)
				directories[i].id = -1;
			directorySlotsAllocated = newCount;
		}
		freeDirectoryCount = directorySlotsAllocated - directoryCount;
		free(freeDirectoryIDs);
		freeDirectoryIDs = typed_malloc(int32_t, freeDirectoryCount);
		freeDirectoryCount = 0;
		for (int i = 0; i < directorySlotsAllocated; i++)
			if (directories[i].id < 0)
				freeDirectoryIDs[freeDirectoryCount++] = i;
		assert(freeDirectoryCount == directorySlotsAllocated - directoryCount);
	}
	directoryCount++;
	int result = freeDirectoryIDs[--freeDirectoryCount];
	directories[result].id = result;
	initializeDirectoryContent(&directories[result].children);
	return result;
} // end of obtainDirectoryID()


void FileManager::releaseDirectoryID(int32_t id) {
	if (cachedDirID == id)
		cachedDirID = -1;
	freeDirectoryContent(&directories[id].children);
	directories[id].id = -1;
	directoryCount--;
} // end of releaseDirectoryID(int32_t)


int32_t FileManager::obtainFileID() {
	if (freeFileCount == 0) {
		freeFileCount = fileSlotsAllocated - fileCount;
		if ((freeFileCount < 0.1 * fileSlotsAllocated) ||
		    (freeFileCount < MINIMUM_SLOT_COUNT)) {
			int newCount = (int)(fileSlotsAllocated * SLOT_GROWTH_RATE);
			if (newCount < fileSlotsAllocated + MINIMUM_SLOT_COUNT)
				newCount = fileSlotsAllocated + MINIMUM_SLOT_COUNT;
			typed_realloc(IndexedFile, files, newCount);
			for (int i = fileSlotsAllocated; i < newCount; i++) {
				files[i].iNode = -1;
				IndexedFileOnDisk ifod;
				ifod.fileName[0] = 0;
				ifod.iNode = -1;
				writeIFOD(i, &ifod);
			}
			fileSlotsAllocated = newCount;
		}
		freeFileCount = fileSlotsAllocated - fileCount;
		free(freeFileIDs);
		freeFileIDs = typed_malloc(int32_t, freeFileCount);
		freeFileCount = 0;
		for (int i = 0; i < fileSlotsAllocated; i++)
			if (files[i].iNode < 0)
				freeFileIDs[freeFileCount++] = i;
		assert(freeFileCount == fileSlotsAllocated - fileCount);
	}
	fileCount++;
	return freeFileIDs[--freeFileCount];
} // end of obtainFileID()


void FileManager::releaseFileID(int32_t id) {
	if (cachedFileID == id)
		cachedFileID = -1;
	fileCount--;
	files[id].iNode = -1;
	IndexedFileOnDisk ifod;
	ifod.iNode = -1;
	writeIFOD(id, &ifod);
} // end of releaseFileID(int32_t)


int32_t FileManager::obtainINodeID() {
	if (biggestINodeID == iNodeSlotsAllocated - 1) {
		if ((iNodeCount < iNodeSlotsAllocated * SLOT_REPACK_THRESHOLD) &&
		    (iNodeCount >= MINIMUM_SLOT_COUNT))
			repackINodes();
		else {
			int newCount = (int)(iNodeSlotsAllocated * SLOT_GROWTH_RATE);
			if (newCount < iNodeSlotsAllocated + MINIMUM_SLOT_COUNT)
				newCount = iNodeSlotsAllocated + MINIMUM_SLOT_COUNT;
			typed_realloc(IndexedINode, iNodes, newCount);
			for (int i = iNodeSlotsAllocated; i < newCount; i++) {
				iNodes[i].hardLinkCount = 0;
				IndexedINodeOnDisk iiod;
				iiod.coreData = iNodes[i];
				writeIIOD(i, &iiod);
			}
			iNodeSlotsAllocated = newCount;
		}
	}
	biggestOffset++;
	if (biggestOffset % FILE_GRANULARITY == 0)
		biggestOffset += FILE_GRANULARITY;
	else
		biggestOffset = (biggestOffset + FILE_GRANULARITY) - (biggestOffset % FILE_GRANULARITY);
	biggestINodeID++;
	iNodes[biggestINodeID].startInIndex = biggestOffset;
	iNodes[biggestINodeID].tokenCount = 0;
	iNodes[biggestINodeID].hardLinkCount = 0;
	return biggestINodeID;
} // end of obtainINodeID()


void FileManager::releaseINodeID(int32_t id) {
	addressSpaceCovered -= iNodes[id].tokenCount;
	if (iNodes[id].hardLinkCount > 0)
		iNodeCount--;
	addToTransactionLog(iNodes[id].startInIndex, iNodes[id].tokenCount, -1);
	iNodes[id].hardLinkCount = 0;
	iNodes[id].tokenCount = 0;
	IndexedINodeOnDisk iiod;
	iiod.coreData = iNodes[id];
	writeIIOD(id, &iiod);
	if (id == biggestINodeID)
		biggestINodeID = id - 1;
	else if ((iNodeCount < iNodeSlotsAllocated * SLOT_REPACK_THRESHOLD) &&
	         (iNodeCount >= MINIMUM_SLOT_COUNT))
		repackINodes();
} // end of releaseINodeID(int32_t)


void FileManager::updateINodeOnDisk(int id) {
	IndexedINodeOnDisk iiod;
	readIIOD(id, &iiod);
	iiod.coreData = iNodes[id];
	iiod.timeStamp = time(NULL);
	writeIIOD(id, &iiod);
} // end of updateINodeOnDisk(int)


void FileManager::repackINodes() {
	bool mustReleaseLock = getLock();
	// assign new IDs to all active INodes
	int32_t *newID = typed_malloc(int32_t, iNodeSlotsAllocated);
	int cnt = 0;
	for (int i = 0; i < iNodeSlotsAllocated; i++) {
		if (iNodes[i].hardLinkCount == 0) {
			newID[i] = -1;
			continue;
		}
		IndexedINodeOnDisk iiod;
		readIIOD(i, &iiod);
		writeIIOD(cnt, &iiod);
		iNodes[cnt] = iNodes[i];
		newID[i] = cnt++;
	}
	if (iNodeCount != cnt)
		fprintf(stderr, "%d != %d\n", iNodeCount, cnt);
	assert(iNodeCount == cnt);
	iNodeCount = cnt;
	biggestINodeID = cnt - 1;

	// reallocate INode memory
	iNodeSlotsAllocated = (int)(iNodeCount * SLOT_GROWTH_RATE);
	if (iNodeSlotsAllocated < MINIMUM_SLOT_COUNT)
		iNodeSlotsAllocated = MINIMUM_SLOT_COUNT;
	typed_realloc(IndexedINode, iNodes, iNodeSlotsAllocated);
	forced_ftruncate(
			iNodeData,
			INODE_FILE_HEADER_SIZE + iNodeSlotsAllocated * sizeof(IndexedINodeOnDisk));

	// initialize/clear all INode descriptors after the part that is in use
	for (int i = iNodeCount; i < iNodeSlotsAllocated; i++) {
		IndexedINodeOnDisk iiod;
		iiod.coreData.hardLinkCount = 0;
		iiod.coreData.tokenCount = 0;
		writeIIOD(i, &iiod);
		iNodes[i].hardLinkCount = 0;
		iNodes[i].tokenCount = 0;
	}

	// update references in all file descriptors
	for (int i = 0; i < fileSlotsAllocated; i++)
		if (files[i].iNode >= 0) {
			files[i].iNode = newID[files[i].iNode];
			IndexedFileOnDisk ifod;
			readIFOD(i, &ifod);
			ifod.iNode = files[i].iNode;
			writeIFOD(i, &ifod);
		}
	free(newID);

	if (mustReleaseLock)
		releaseLock();
} // end of repackINodes()


void FileManager::getFileAndDirectoryCount(int *fileCount, int *directoryCount) {
	bool mustReleaseLock = getLock();
	*fileCount = this->fileCount;
	*directoryCount = this->directoryCount;
	if (mustReleaseLock)
		releaseLock();
} // end of getFileAndDirectoryCount(int*, int*)


void FileManager::getClassName(char *target) {
	strcpy(target, "FileManager");
}


/******************** Security stuff starts here. ********************/


static int gidComparator(const void *a, const void *b) {
	gid_t *x = (gid_t*)a;
	gid_t *y = (gid_t*)b;
	if (*x < *y)
		return -1;
	else if (*x > *y)
		return +1;
	else
		return 0;
} // end of gidComparator(const void*, const void*)


gid_t * FileManager::computeGroupsForUser(uid_t userID, int *groupCount) {
	char line[4096], *temp;
	char userName[1024];
	gid_t *groupList = typed_malloc(gid_t, 32);
	int maxGroupCount = 32;
	*groupCount = 0;
	userName[0] = 0;

	// we do not use the getgrent/getpwent functions, because they are not
	// thread-safe, but we may have multiple FileManager instances working
	// in parallel

	// scan the /etc/passwd file in order to obtain the username and the
	// guy's primary group
	FILE *passwdFile = fopen("/etc/passwd", "r");
	if (passwdFile != NULL) {
		while (fgets(line, 4094, passwdFile) != NULL) {
			if (strlen(line) < 3)
				continue;
			line[strlen(line) - 1] = 0;
			StringTokenizer *tok = new StringTokenizer(line, ":");
			char *name = tok->getNext();
			tok->getNext(); // skip password
			char *uid = tok->getNext();
			char *pGrp = tok->getNext();
			if (pGrp != NULL)
				if (userID == (uid_t)atoi(uid)) {
					strcpy(userName, name);
					groupList[*groupCount] = atoi(pGrp);
					*groupCount = *groupCount + 1;
				}
			delete tok;
		}
		fclose(passwdFile);
	} // end if (passwdFile != NULL)

	// scan the /etc/group file
	FILE *groupFile = fopen("/etc/group", "r");
	if (groupFile != NULL) {
		while (fgets(line, 4094, groupFile) != NULL) {
			if (strlen(line) < 3)
				continue;
			line[strlen(line) - 1] = 0;
			StringTokenizer *tok = new StringTokenizer(line, ":");
			tok->getNext();
			tok->getNext();
			if ((temp = tok->getNext()) == NULL) {
				delete tok;
				continue;
			}
			gid_t groupID = atoi(temp);
			if ((temp = tok->getNext()) == NULL) {
				delete tok;
				continue;
			}
			StringTokenizer *tok2 = new StringTokenizer(temp, ",");
			while (tok2->hasNext()) {
				if (strcmp(userName, tok2->getNext()) == 0) {
					if (*groupCount >= maxGroupCount) {
						maxGroupCount *= 2;
						typed_realloc(gid_t, groupList, maxGroupCount);
					}
					groupList[*groupCount] = groupID;
					*groupCount = *groupCount + 1;
					break;
				}
			}
			delete tok2;
			delete tok;
		}
		fclose(groupFile);
	} // end if (groupFile != NULL)

	// sort and remove duplicates
	qsort(groupList, *groupCount, sizeof(gid_t), gidComparator);
	int outPos = 1;
	for (int i = 1; i < *groupCount; i++)
		if (groupList[i] != groupList[i - 1])
			groupList[outPos++] = groupList[i];
	return groupList;
} // end of computeGroupsForUser(uid_t, int*)


bool FileManager::userIsInGroup(gid_t groupID, gid_t *groupList, int groupCount) {
	if (groupCount == 0)
		return false;
	if (groupList[0] == groupID)
		return true;
	int lower = 0;
	int upper = groupCount - 1;
	while (upper > lower) {
		int middle = (lower + upper) / 2;
		if (groupList[middle] == groupID)
			return true;
		else if (groupList[middle] < groupID)
			lower = middle + 1;
		else
			upper = middle - 1;
	}
	return false;
} // end of userIsInGroup(gid_t, gid_t*, int)


void FileManager::recursivelyMarkVisibleExtents(int dirID, VisibleExtent *result,
			uid_t userID, gid_t *groups, int groupCount) {

	if ((userID == Index::SUPERUSER) || (userID == Index::GOD))
		goto directoryPermissionOk;

	// check if user is dir owner; if yes, apply restrictions
	if (userID == directories[dirID].owner) {
		if ((directories[dirID].permissions & S_IRUSR) && (directories[dirID].permissions & S_IXUSR))
			goto directoryPermissionOk;
		else
			return;
	}

	// check if user is group member; if yes, apply restrictions
	if (userIsInGroup(directories[dirID].group, groups, groupCount)) {
		if ((directories[dirID].permissions & S_IRGRP) && (directories[dirID].permissions & S_IXGRP))
			goto directoryPermissionOk;
		else
			return;
	}

	// if user is neither owner nor group member, apply EVERYBODY restrictions
	if ((directories[dirID].permissions & S_IROTH) && (directories[dirID].permissions & S_IXOTH))
		goto directoryPermissionOk;
	else
		return;

directoryPermissionOk:

	for (int lc = 0; lc < 2; lc++) {
		// we have to do this loop twice: once for the long list, once for the short list

		DC_ChildSlot *children;
		int childCount;
		if (lc == 0) {
			children = directories[dirID].children.longList;
			childCount = directories[dirID].children.longAllocated;
		}
		if (lc == 1) {
			children = directories[dirID].children.shortList;
			childCount = directories[dirID].children.shortCount;
		}

		for (int i = 0; i < childCount; i++) {
			int id = children[i].id;
			if (id == DC_EMPTY_SLOT)
				continue;

			if (id < 0)
				recursivelyMarkVisibleExtents(-id, result, userID, groups, groupCount);
			else {
				int iNode = files[id].iNode;
				if (iNode < 0)
					continue;
				assert(iNode <= biggestINodeID);
				// check if file is already marked as visible; if so, continue with next child
				if (result[iNode].fileID >= 0)
					continue;

				if ((userID == Index::SUPERUSER) || (userID == Index::GOD))
					goto filePermissionOk;

				// check if user is file owner
				if (userID == iNodes[iNode].owner) {
					if (iNodes[iNode].permissions & S_IRUSR)
						goto filePermissionOk;
					else
						continue;
				}

				// check if user is group member
				if (userIsInGroup(iNodes[iNode].group, groups, groupCount)) {
					if (iNodes[iNode].permissions & S_IRGRP)
						goto filePermissionOk;
					else
						continue;
				}

				// apply rules for EVERYBODY
				if (iNodes[iNode].permissions & S_IROTH)
					goto filePermissionOk;
				else
					continue;

filePermissionOk:

				result[iNode].fileID = id;
			}
		} // end for (int i = 0; i < directories[dirID].children.count; i++)

	} // end for (int lc = 0; lc < 2; lc++)

} // end of recursivelyMarkVisibleExtents(...)


VisibleExtent * FileManager::getVisibleFileExtents(uid_t userID, int *listLength) {
	LocalLock lock(this);
	bool mustReleaseLock = getLock();

	if (biggestINodeID < 0) {
		*listLength = 0;
		return typed_malloc(VisibleExtent, 1);
	}

	VisibleExtent *result = typed_malloc(VisibleExtent, biggestINodeID + 1);
	for (int i = 0; i <= biggestINodeID; i++)
		result[i].fileID = -1;
	if (userID == Index::GOD) {
		// if we are God, we just mark every INode as readable
		recursivelyMarkVisibleExtents(0, result, userID, NULL, 0);
	}
	else {
		// if we are not God, we have to be more careful and actually go and find
		// out which INodes may be read
		int groupCount;
		gid_t *groups = computeGroupsForUser(userID, &groupCount);
		recursivelyMarkVisibleExtents(0, result, userID, groups, groupCount);
		free(groups);
	}
	int outCnt = 0;
	for (int i = 0; i <= biggestINodeID; i++) {
		if (result[i].fileID >= 0) {
			result[i].startOffset = iNodes[i].startInIndex;
			result[i].tokenCount = iNodes[i].tokenCount;
			result[i].documentType = iNodes[i].documentType;
			result[outCnt++] = result[i];
		}
	}

	for (int i = 1; i < outCnt; i++) {
		if (result[i].startOffset <= result[i - 1].startOffset) {
			fprintf(stderr, "(%d) " OFFSET_FORMAT " <= " OFFSET_FORMAT "\n",
					i, result[i].startOffset, result[i - 1].startOffset);
		}
		assert(result[i].startOffset > result[i - 1].startOffset);
	}

	*listLength = outCnt;

	if (outCnt == 0) {
		free(result);
		return NULL;
	}

	if (outCnt < biggestINodeID)
		typed_realloc(VisibleExtent, result, outCnt);

	return result;
} // end of getVisibleFileExtents(uid_t, int*)


/******************** Security stuff ends here. ********************/


