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
 * The FileManager class is used to keep track of the structure of the
 * file system (links, inodes, directories).
 *
 * author: Stefan Buettcher
 * created: 2005-02-18
 * changed: 2009-02-01
 **/


#ifndef __FILEMANAGER__FILEMANAGER_H
#define __FILEMANAGER__FILEMANAGER_H


#include "data_structures.h"
#include "../index/index_types.h"
#include "../misc/lockable.h"


class Index;

typedef struct {
	offset startOffset;
	uint32_t tokenCount;
	int32_t delta;
} AddressSpaceChange;


class FileManager : public Lockable {

	friend class Index;

public:

	static const int INITIAL_TRANSACTION_LOG_SPACE = 8;

	/**
	 * By default, at least this many slots are allocated for directories, files,
	 * and INodes.
	 **/
	static const int MINIMUM_SLOT_COUNT = 1024;

	/**
	 * If we run out of slots, we reallocate the slot memory. This is the growth
	 * rate used for reallocation.
	 **/
	static const double SLOT_GROWTH_RATE = 1.23;

	/**
	 * If the relative number of used slots of a type (dirs, files, inodes) becomes
	 * smaller than this, we repack the respective array in order to save memory.
	 **/
	static const double SLOT_REPACK_THRESHOLD = 0.78;

	static const off_t INODE_FILE_HEADER_SIZE = 2 * sizeof(int32_t) + sizeof(offset);
	
	static const char *LOG_ID;

private:

	Index *owner;

	/** File names of our data files. **/
	char *directoryDataFile, *fileDataFile, *iNodeDataFile;

	/** File handles of the data files. **/
	int directoryData, fileData, iNodeData;

	/** For increased performance, we cache the most recently accessed file. **/
	int cachedFileID;
	char cachedFileName[256];

	/** The same goes for the most recently accessed directory. **/
	int cachedDirID;
	char cachedDirName[256];

	/**
	 * The mount point below which the directory tree described here lives.
	 * Initially, this is "/". Can be changed by calling setMountPoint.
	 **/
	char mountPoint[256];

	/** Number of directories managed by the FileManager. **/
	int32_t directoryCount;

	/** Number of slots (IndexedDirectory instances) allocated. **/
	int32_t directorySlotsAllocated;

	/** A list of all directories that we know of. **/
	IndexedDirectory *directories;

	/** Number of free directories in the "freeDirectoryIDs" array. **/
	int32_t freeDirectoryCount;

	/**
	 * This array contains a list of free directory IDs. These are used to assign
	 * IDs to new directories without having to do a linear scan over all allocated
	 * directory slots.
	 **/
	int32_t *freeDirectoryIDs;

	/** Number of files managed by the FileManager. **/
	int32_t fileCount;

	/** Number of slots (IndexedFile instances) allocated. **/
	int32_t fileSlotsAllocated;

	/** A list of all files (hard links) that we know of. **/
	IndexedFile *files;

	/** Number of free files in the "freeFileIDs" array. **/
	int32_t freeFileCount;

	/** List of free (unused) file IDs. **/
	int32_t *freeFileIDs;

	/** Number of INodes in the system. **/
	int32_t iNodeCount;

	/** Number of INode slots (IndexedINode instances) allocated. **/
	int32_t iNodeSlotsAllocated;

	/** ID of the INode that was least recently added to the FileManager. **/
	int32_t biggestINodeID;

	/**
	 * A list of all INodes we know of. INodes have to be sorted in increasing
	 * index position within this list. This makes it impossible to simply re-use
	 * the IDs of deleted INodes. Instead, when we see that
	 *
	 *   iNodeCount <= iNodeSlotsAllocated * 0.8,
	 *
	 * we completely reassign all INode IDs, changing the references inside
	 * all IndexedFile instances.
	 **/
	IndexedINode *iNodes;

	static const int HASHTABLE_SIZE = 50021;

	/**
	 * We maintain a hashtable that tells us which FileManager-specific INode ID belongs
	 * to a certain file-system-specific INode ID. Collisions are solved using linked lists.
	 **/
	int32_t iNodeHashtable[HASHTABLE_SIZE];

	/**
	 * The biggest offset we have seen so far. Is used to ensure that INodes
	 * are always sorted in increasing order.
	 **/
	offset biggestOffset;

	/**
	 * Tells us what big a part of the total index address space is covered by files.
	 * This needs to be updated whenever a file is added, changed, or removed.
	 **/
	offset addressSpaceCovered;

	/**
	 * A list of AddressSpaceChange instances giving a summary of what was going
	 * on during the current transaction.
	 **/
	AddressSpaceChange *transactionLog;

	/** Number of elements in the transaction log. **/
	int transactionLogSize, transactionLogAllocated;

public:

	/**
	 * Creates a new FileManager instance. Depending in the value of "create",
	 * the FileManager is either empty or contains the data found in the
	 * workDirectory specified.
	 **/
	FileManager(Index *owner, const char *workDirectory, bool create);

	/** Saves data to disk and free all memory occupied so far. **/
	~FileManager();

	/** Saves all data to disk: "index.directories", "index.files", "index.inodes". **/
	void saveToDisk();

	/**
	 * Adds the file given by its full path to the list of files managed by the
	 * FileManager. If we already know the INode that this file points at, we
	 * simply create a new hard link to the INode. Otherwise, we create a new
	 * (empty) INode first.
	 * Returns the start offset of the index address range occupied by the file
	 * or -1 if the file could not be added to the FileManager's internal data
	 * structures. Failure means illegal path name (not below mount point or 1,
	 * more components too long, or no more free resources for the new file).
	 **/
	offset addFile(char *fullPath, int16_t documentType, int16_t language);

	/**
	 * If a file has been changed and is re-indexed, this has to be reflected
	 * inside the FileManager in some way. changeFileContent is used to do that.
	 * The method will automatically call updateFileAttributes and update all
	 * internal data (timeStamp etc.).
	 * "reservedTokenCount" is used to define the amount of index address space
	 * reserved for this file so that we can perform append operations later on.
	 * "reservedTokenCount" may only be set if the given file is at the end of
	 * the address space. Otherwise, we might run into the address space occupied
	 * by another file, and bad things will happen.
	 **/
	void changeTokenCount(char *fullPath, uint32_t tokenCount, uint32_t reservedTokenCount);

	/**
	 * Removes the file given by its full path from the list of files. If this
	 * file was the last pointer to an INode instance, the INode is removed as
	 * well.
	 **/
	bool removeFile(char *fullPath);

	/**
	 * Removes the directory given by "fullPath" and deletes all its descendants
	 * (directories and files).
	 **/
	bool removeDirectory(char *fullPath);

	/**
	 * Updates the file attributes of the file given by performing a stat
	 * operation and storing the data obtained inside the INode descriptor that
	 * the file points at.
	 **/
	void updateFileAttributes(char *fullPath);

	/**
	 * Updates the attributes (access permissions) for the directory given by
	 * "fullPath".
	 **/
	void updateDirectoryAttributes(char *fullPath);

	/**
	 * This method is used to move a file or directory within the file system
	 * directory structure. A move operation can either be a simple rename or an
	 * actual move (i.e. from one directory to another).
	 **/
	bool renameFileOrDirectory(char *oldPath, char *newPath);

	/**
	 * Checks all files currently in the index. Every file that does not exist in
	 * the file system any more is removed from the internal data structures of
	 * the indexing system.
	 **/
	void removeAllInexistentFiles();

	/**
	 * Returns true iff the file given by "fullPath" has been changed (checked by
	 * doing a stat and looking at the modification time) since we updated the
	 * file data the last time. We use this information during full file system
	 * scans.
	 **/
	bool changedSinceLastUpdate(char *fullPath);

	/**
	 * Returns the full path of the directory given by "id". Memory has to be freed
	 * by the caller.
	 **/
	char *getDirectoryPath(int32_t id);

	/**
	 * Returns the full path of the file given by "id". Memory has to be freed by
	 * the caller.
	 **/
	char *getFilePath(int32_t id);

	/**
	 * Fills complete INode info for the *file* specified by "fileID" into the
	 * IndexedINodeOnDisk structure given by "iiod". Returns true if the INode exists,
	 * false otherwise.
	 **/
	bool getINodeInfo(int32_t fileID, IndexedINodeOnDisk *iiod);

	/** Same as above, but for a given file name instead of a file ID. **/
	bool getINodeInfo(const char *fullPath, IndexedINodeOnDisk *iiod);

	/** Reads the on-disk file descriptor for the file with the given ID into "ifod". **/
	bool readIFOD(int32_t fileID, IndexedFileOnDisk *ifod);

	/**
	 * Returns a list of index extents, representing the set of files that may
	 * be searched by the user given by "userID". This method is called by the
	 * query processor once per query. The result is used to apply security
	 * restrictions to the index data.
	 **/
	VisibleExtent *getVisibleFileExtents(uid_t userID, int *listLength);

	/**
	 * Returns true iff the given user may access the given file and the file is
	 * in the index.
	 **/
	bool mayAccessFile(uid_t userID, const char *fullPath);

	/**
	 * Returns the biggest offset value in any of the files (or INodes, rather) managed
	 * by this FileManager instance.
	 **/
	offset getBiggestOffset();

	/**
	 * Puts the number of files and directories managed by this FileManager into
	 * "fileCount" and "directoryCount", respectively.
	 **/
	void getFileAndDirectoryCount(int *fileCount, int *directoryCount);

	/**
	 * The whole directory structure managed by the FileManager is relative to a
	 * given mount point. The path of this mount point can be changed by calling
	 * this method. Initially, this is "/". Return value is "true" if the operation
	 * was successful, "false" otherwise (path too long).
	 **/
	bool setMountPoint(const char *newMountPoint);

	/**
	 * Returns the mount point of this FileManager object. Memory has to be freed
	 * by the caller.
	 **/
	char *getMountPoint();

public:

	/**
	 * Returns true iff the user with given userID and given group memberships
	 * may access the file defined by permissions, fileOwner, and fileGroup.
	 **/
	static bool mayAccessFile(uint16_t permissions, uid_t fileOwner,
			gid_t fileGroup, uid_t userID, gid_t *groups, int groupCount);

	/**
	 * Returns true iff the user with given userID and given group memberships
	 * may access the directory defined by permissions, fileOwner, and fileGroup.
	 **/
	static bool mayAccessDirectory(uint16_t permissions, uid_t fileOwner,
			gid_t fileGroup, uid_t userID, gid_t *groups, int groupCount);

	/**
	 * Returns the hash value of the string given by "s". The hash value is used
	 * to speed up search operations. Files inside a directory are sorted by their
	 * hash value.
	 **/
	static int32_t getHashValue(char *s);

protected:

	/**
	 * Begins a new transaction on the FileManager's data. This transaction cannot
	 * be interrupted by any other process.
	 **/
	void beginTransaction();

	/**
	 * When a transaction is finished, the lock is released, and the owner of the
	 * FileManager object is notified of all address space changes that have taken
	 * place during the transaction.
	 **/
	void finishTransaction();

	void addToTransactionLog(offset startOffset, uint32_t tokenCount, int32_t delta);

	void getClassName(char *target);

private:

	/** Does some counting, checking whether in-memory and on-disk data are consistent. **/
	void sanityCheck();

	/** Writes the file descriptor for the given file ID to disk. **/
	void writeIFOD(int id, IndexedFileOnDisk *ifod);

	void readIIOD(int id, IndexedINodeOnDisk *iiod);

	void writeIIOD(int id, IndexedINodeOnDisk *iiod);

	/**
	 * Collapses the given path name and checks if all its components obey the
	 * length restrictions we defined. If everything is ok, the collapsed path
	 * name is returns. If not, NULL.
	 **/
	char *toCanonicalForm(const char *path);

	/**
	 * Does something like "/home/user/file" -> "/user/file", if "/home" is the
	 * mountPoint of the FileManager instance. Memory has to be freed by the caller.
	 * If the given path is not below the mount point, the method returns NULL.
	 **/
	char *makeRelativeToMountPoint(const char *fullPath);

	int32_t obtainDirectoryID();
	
	void releaseDirectoryID(int32_t id);

	int32_t obtainFileID();

	int32_t obtainINodeID();

	void releaseFileID(int32_t id);

	void releaseINodeID(int32_t id);

	void recursivelyMarkVisibleExtents(int dirID, VisibleExtent *result);

	int32_t getDirectoryID(int32_t dir, char *name, bool createOnDemand);

	int32_t getDirectoryID(char *relPath, bool createOnDemand);

	int32_t getFileID(int32_t dir, char *name, bool createOnDemand);

	int32_t getFileID(char *relPath, bool createOnDemand);

	void addDirectoryToDirectory(int id, int parent);

	void removeDirectoryFromDirectory(int id, int parent);

	void addFileToDirectory(int id, int parent);

	void removeFileFromDirectory(int id, int parent);

	void removeDirectory(int directoryID);

	void removeNonEmptyDirectory(int directoryID);

	void removeFile(int fileID);

	void repackINodes();

private:

	/** Returns true iff the "groupID" appears in "groupList". **/
	static bool userIsInGroup(gid_t groupID, gid_t *groupList, int groupCount);

	static gid_t * computeGroupsForUser(uid_t userID, int *groupCount);

	/** Security stuff. **/
	void recursivelyMarkVisibleExtents(int dirID, VisibleExtent *result,
			uid_t userID, gid_t *groups, int groupCount);

	/**
	 * Returns the hash value for the file-system-specific INode ID given by
	 * "fsID". Hash value is always non-negative.
	 **/
	static int32_t getINodeHashValue(ino_t fsID);

	/**
	 * Returns the ID of the IndexedINode object that describes the INode with
	 * file-system-specific ID "fsID". -1 if there is no such IndexedINode object.
	 **/
	int getINode(ino_t fsID);

	/** Used to speed up the update operation when the file ID is already known. **/
	bool updateFileAttributes(char *relPath, int fileID);

	/** Copies the in-memory info to the on-disk stuff. **/
	void updateINodeOnDisk(int id);

	/** Releases all allocated resources. Called from within the destructor. **/
	void freeMemory();

}; // end of class FileManager


#endif


