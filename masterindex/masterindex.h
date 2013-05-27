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
 * Definition of the MasterIndex class. MasterIndex is a daemon process that
 * sits in the system and watches for events (file changes, mounts, ...). It
 * creates new Index instances when the administrator asks for it or when a
 * file system is mounted, deleted Index instances when UMOUNT is requested,
 * and much more.
 *
 * author: Stefan Buettcher
 * created: 2005-03-09
 * changed: 2007-12-17
 **/


#ifndef __MASTER__MASTERINDEX__H
#define __MASTER__MASTERINDEX__H


#include "../index/index.h"
#include "../daemons/authconn_daemon.h"


class MasterIndex : public Index {

	friend class MasterDocIdCache;
	friend class MasterVE;

public:

	/** Maximum number of simultaneously mounted file systems supported. **/
	static const int MAX_MOUNT_COUNT = 100;

	/** Maximum number of files per sub-index. **/
	static const int MAX_FILES_PER_INDEX = 20000000;

	/** Maximum number of directories per sub-index. **/
	static const int MAX_DIRECTORIES_PER_INDEX = 20000000;

	/**
	 * Every sub-index has its own index range. To avoid collisions, we give a
	 * relatively large range to every sub-index: 10^12.
	 **/
	static const offset MAX_INDEX_RANGE_PER_INDEX = 10000000000000LL;

	/** Tells us whether the MasterIndex has been started successfully. **/
	bool startupOk;

protected:

	/** Number of active mounts in the entire system. **/
	int activeMountCount;

	/** Path names of the active mounts. Empty slots are NULL. **/
	char *mountPoints[MAX_MOUNT_COUNT];

	/** Number of Index instances we control. **/
	int indexCount;

	/**
	 * List of indexes, each index corresponds to an active mount point, but
	 * not every mount point needs to have an index.
	 **/
	Index *subIndexes[MAX_MOUNT_COUNT];

	/**
	 * This array tells us for every sub-index whether an UMOUNT operation for the
	 * associated file system has been requested. If not, the value in the array
	 * is -1. Otherwise, the value is the time at which the umount has been
	 * requested. The "registeredUsers" array in the Index class tells us at what
	 * point in time the currently active queries have started looking at index
	 * data. If all active queries have begun after the point when the UMOUNT was
	 * requested, we remove the sub-index. Okidoki?
	 * In order to avoid waiting until infinity, we do not use an index for which
	 * UMOUNT was requested to serve a new user query.
	 **/
	int64_t unmountRequested[MAX_MOUNT_COUNT];

public:

	/**
	 * Creates a new MasterIndex with authconn file in the given directory.
	 * The MasterIndex will use fschange (if not available: inotify) to keep
	 * track of file system changes.
	 **/
	MasterIndex(const char *directory);

	/**
	 * Creates a new MasterIndex that controls the sub-indices defined by
	 * the directories given via "subIndexDirs". This MasterIndex will not listen
	 * for file changes nor create an authconn file.
	 **/
	MasterIndex(int subIndexCount, char **subIndexDirs);

	~MasterIndex();

	/** See Index class for documentation. **/
	virtual int notify(const char *event);

	/** See Index class for documentation. **/
	virtual ExtentList *getPostings(const char *term, uid_t userID);

	/** See Index class for documentation. **/
	virtual ExtentList *getPostings(const char *term, uid_t userID, bool fromDisk, bool fromMemory);

	/** See Index class for documentation. **/
	virtual void getPostings(const char **terms, int termCount, uid_t userID, ExtentList **results);

	/** See Index class for documentation. **/
	virtual void addAnnotation(offset position, const char *annotation);

	/** See Index class for documentation. **/
	virtual void getAnnotation(offset position, char *buffer);

	/** See Index class for documentation. **/
	virtual void removeAnnotation(offset position);

	/** See Index class for documentation. **/
	virtual offset getBiggestOffset();

	/** See Index class for documentation. **/
	virtual int getDocumentType(const char *fullPath);

	/** See Index class for documentation. **/
	virtual bool getLastIndexToTextSmallerEq(offset where, offset *indexPosition, off_t *filePosition);

	/** See Index class for documentation. **/
	virtual VisibleExtents *getVisibleExtents(uid_t userID, bool merge);

	/** See Index class for documentation. **/
	virtual void getDictionarySize(offset *lowerBound, offset *upperBound);

	/** See Index class for documentation. **/
	virtual void deregister(int64_t id);

	/** See Index class for documentation. **/
	virtual void getIndexSummary(char *buffer);

	virtual IndexCache *getCache() { return NULL; }

	virtual DocumentCache *getDocumentCache(const char *fileName);

	/** Returns true iff the given user may access (read) the given file. **/
	virtual bool mayAccessFile(uid_t userID, const char *path);

	virtual void sync();

protected:

	/** See Index class for documentation. **/
	virtual int64_t registerForUse(int64_t suggestedID);

private:

	/**
	 * One of the problems we have to deal with are symbolic links to directories.
	 * Every time a file system change notification comes in, we have to transform
	 * symolic links into their real directory names. This is done by this method.
	 * It returns the transformed, canonical path name. Memory has to be freed by
	 * the caller.
	 **/
	static char *resolveSymbolicLinks(const char *event);

	/**
	 * Given an absolute path, this method returns the ID of the mount point that
	 * is responsible for the path, or -1 if no responsible mount point can be found.
	 * Be careful when you use the return value, as there is not necessarily an
	 * index associated with the mount point.
	 **/
	int getMountPointForPath(const char *path);

	/**
	 * Creates a new Index instance that will be responsible for the file system
	 * mounted below the given path. There are a couple of exceptions, however:
	 *   (1) If MAX_MOUNT_COUNT is reached, we do not create a new Index.
	 *   (2) If the file system is read-only, we do not create a new Index.
	 **/
	void createSubIndexForMountPoint(const char *path);

	/**
	 * Returns true iff it is allowed to index the file system rooted at
	 * "mountPoint". Checks for the existence of the .index_disallow file as well
	 * as the content of the configuration file.
	 **/
	bool mayIndexThisFileSystem(const char *mountPoint);

}; // end of class MasterIndex


#endif



