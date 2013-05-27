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
 * Definition of the class FileSystem. FileSystem is used to facilitate
 * file storage on disk. A file-in-a-file approach is chosen to store posting
 * lists and such. This way, we can easily put every posting list into a
 * separate file, which would be problematic (>1000000 terms) if we used the
 * OS filesystem.
 *
 * author: Stefan Buettcher
 * created: 2004-09-03
 * changed: 2004-11-07
 **/


#ifndef __FILESYSTEM__FILESYSTEM_H
#define __FILESYSTEM__FILESYSTEM_H


#include "../misc/lockable.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>


// possible return values for functions with status code
static const int FILESYSTEM_SUCCESS = 0;
static const int FILESYSTEM_ERROR = -1;

// working modes for FileSystemCache objects
static const int FILESYSTEMCACHE_LRU = 1;
static const int FILESYSTEMCACHE_FIFO = 2;


class File;
class FileSystem;
class FileSystemCache;


typedef int32_t fs_pageno;
typedef int32_t fs_fileno;
typedef unsigned char byte;


/**
 * The FileSystem class represents a virtual filesystem. Inside this filesystems,
 * instances of the File class can live and do whatever they want to.
 **/
class FileSystem : public Lockable {

	friend class File;
	friend class FileSystemCache;

public:

	/**
	 * Parameter passed to open(2) when opening the file. I would prefer to have
	 * O_RDWR | O_SYNC here, but probably it is more efficient to have O_RDWR only.
	 **/
	static const int FILESYSTEM_ACCESS = O_RDWR;

public:

	// default values, can be used to create new FileSystem objects (suggestions)
	static const int DEFAULT_PAGE_COUNT = 1024;
	static const int DEFAULT_PAGE_SIZE = 1024;

	/** These are minimum and maximum values that may not be exceeded. **/
	static const int MIN_PAGE_COUNT = 32;
	static const int MIN_PAGE_SIZE = 128;
	static const int MAX_PAGE_SIZE = 8192;
	static const int MAX_PAGE_COUNT = 1073741824;

	/** A filesystem with less than this many pages is considered "small". **/
	static const int SMALL_FILESYSTEM_THRESHOLD = 1024;

	/**
	 * If this is found in the page layout file, it indicates that the page
	 * is empty.
	 **/
	static const int32_t UNUSED_PAGE = -123456789;
	static const int32_t USED_PAGE = -987654321;

	/** Have a default cache size of 1 MB per filesystem. **/
	static const int DEFAULT_CACHE_SIZE = 1 * 1024 * 1024;

private:

	/**
	 * If we are loading a filesystem, we want to make sure it really is a filesystem.
	 * The first 32 bit of the file always have to be the value of FINGERPRINT.
	 **/
	static const int32_t FINGERPRINT = 912837123;

	/** Length of the preamble of the filesystem (fingerprint, pagesize, pagecount). **/
	static const int PREAMBLE_LENGTH = 5 * sizeof(int32_t);

	/** Length of one int pointer, as stored on disk. **/
	static const int INT_SIZE = sizeof(int32_t);

	/** Number of pages in the filesystem. **/
	int32_t pageCount;

	/** Size (in bytes) of each page in the filesystem. **/
	int32_t pageSize;
	int32_t intsPerPage, doubleIntsPerPage;

	/** Number of pages occupied by page layout table. **/
	int32_t pageLayoutSize;

	/** Number of pages occupied by file->page mappings. **/
	int32_t fileMappingSize;

	/**
	 * Reports for every page in the page layout table how many free pages in the
	 * filesystem it references.
	 **/
	int16_t *freePages;

	/**
	 * Reports for every page in the file->page mappings table how many free file
	 * slots it contains that can be occupied by new files.
	 **/
	int16_t *freeFileNumbers;

protected:
	
	/**
	 * File handle of the file that the filesystem sits in or -1 if something went
	 * wrong in the class constructor.
	 **/
	int dataFile;

	/** Name of the data file. **/
	char *dataFileName;

	/** In case we do some caching to speed up things. **/
	FileSystemCache *cache;

	/** Size of the cache, as specified when calling the constructor. **/
	int32_t cacheSize;

	/** These are for evaluating cache efficiency. **/
	int64_t cachedReadCnt, uncachedReadCnt;
	int64_t cachedWriteCnt, uncachedWriteCnt;

public:

	/** Loads a virtual filesystem from the file given by parameter "fileName". **/
	FileSystem(char *fileName);

	/**
	 * Creates a new virtual filesystem with certain parameters. A filesystem
	 * consists of a number of pages. The size of each page is given by parameter
	 * "pageSize". The initial number of pages (>0) is given by parameter "pageCount".
	 **/
	FileSystem(char *fileName, int pageSize, fs_pageno pageCount);

	/**
	 * Similar to the above constructor, but you can also specify the size of the
	 * cache (in bytes).
	 **/
	FileSystem(char *fileName, int pageSize, fs_pageno pageCount, int cacheSize);

	/** Frees all resources occupied by the FileSystem object. **/
	~FileSystem();

	/**
	 * Returns an array that contains all file->page mappings for the filesystem.
	 * Memory occupied has to be freed by the caller. "arraySize" is used to store
	 * the size of the array returned. We need this in order to avoid segmentation
	 * faults.
	 **/
	int32_t *getFilePageMapping(int *arraySize);

private:

	void init(char *fileName, int pageSize, fs_pageno pageCount, int cacheSize);

public:

	/** Returns the name of the data file. Do not touch it, caller! **/
	char *getFileName();

	/** Returns true iff the FileSystem instance represents an active filesystem. **/
	bool isActive();

	/**
	 * Runs a defragmentation algorithm on the filesystem. The return value is
	 * either FILESYSTEM_SUCCESS or FILESYSTEM_ERROR.
	 **/
	int defrag();

	/**
	 * Changes the size of the filesystem (number of pages) to the value given by
	 * parameter "newPageCount". Return value is FILESYSTEM_SUCCESS if the size
	 * change is successful.
	 **/
	int changeSize(fs_pageno newPageCount);

	/**
	 * Deletes the file specified by "fileHandle", if existent. Returns
	 * FILESYSTEM_SUCCESS or FILESYSTEM_ERROR, depending on what happened.
	 **/
	int deleteFile(fs_fileno fileHandle);

	/**
	 * Creates a new file. Returns the file handle of the new file or FILESYSTEM_ERROR
	 * if something went wrong. The parameter "fileno" can be used to specify the file
	 * number (handle) of the file to be created. If it is -1, an arbitrary number is
	 * chosen. If a file with number "fileno" already exists, FILESYSTEM_ERROR is returned.
	 **/
	fs_fileno createFile(fs_fileno fileno);

	/**
	 * Returns the first page of the file specified by "fileHandle" or
	 * FILESYSTEM_ERROR if the file does not exist.
	 **/
	fs_pageno getFirstPage(fs_fileno fileHandle);

	/** Returns the page size of the filesystem. **/
	int getPageSize();

	/** Returns the number of pages in the filesystem. **/
	int getPageCount();

	/** Returns the number of used pages in the filesystem. **/
	int getUsedPageCount();

	/** Returns the number of files in the filesystem. **/
	int getFileCount();

	/** Returns the size of the filesystem. **/
	off_t getSize();

	/**
	 * Reads the page specified by "pageNumber" into the buffer referenced by
	 * "buffer". Returns FILESYSTEM_SUCCESS or FILESYSTEM_ERROR.
	 **/
	int readPage(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer);
	int readPage(fs_pageno pageNumber, void *buffer);

	/** Counterpart to readPage(int). **/
	int writePage(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer);
	int writePage(fs_pageno pageNumber, void *buffer);

	void getCacheEfficiency(int64_t *reads, int64_t *uncachedReads,
	                        int64_t *writes, int64_t *uncachedWrites);

protected:

	/** This method reads data directly from the data file. No caching. **/
	int readPage_UNCACHED(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer);

	/** This method writes data directly to the data file. No caching. **/
	int writePage_UNCACHED(fs_pageno pageNumber, int32_t offset, int32_t count, void *buffer);
	
	/**
	 * Returns the number of pages that are occupied by the file specified by
	 * "fileHandle".
	 **/
	fs_pageno getPageCount(fs_fileno fileHandle);

	/** Counterpart to getPageCount(fs_fileno). **/
	int setPageCount(fs_fileno fileHandle, fs_pageno newPageCount);

	fs_pageno getPageStatus(fs_pageno page);

	int setPageStatus(fs_pageno page, fs_pageno newStatus);

	/**
	 * Returns the offset number of a free page in the filesystem. It tries to
	 * find a page that is close to the one given by "closeTo". If "closeTo" is < 0,
	 * it just returns any free page. FILESYSTEM_ERROR if an error occurred.
	 **/
	fs_pageno claimFreePage(fs_fileno owner, fs_pageno closeTo);

public:

	/** Writes all pending updates to disk and erases the content of the cache. **/
	void flushCache();

private:

	void initializeFreeSpaceArrays();

	/** Returns true iff the page given by "pageNumber" contains free storage space. **/
	bool isPageFree(fs_pageno pageNumber);

	/** Writes data to the file->page mappings table. **/
	int setFirstPage(fs_fileno fileHandle, fs_pageno firstPage);

	/** Reserves and returns a file number that is currently unused. **/
	fs_fileno claimFreeFileNumber();

	/**
	 * Increases the size of the file mappings table by 1 page. This operation is
	 * performed when no free file slot can be found by createFile.
	 **/
	int increaseFileMappingSize();

	/**
	 * Decreases the size of the file mappings table by 1 page. This operation is
	 * performed when deleteFile detects that the last 2 pages of the table are
	 * empty.
	 **/
	int decreaseFileMappingSize();

	/** Turns the disk cache on. **/
	void enableCaching();

	/** Turns the disk cache off. Pending data is written to disk. **/
	void disableCaching();

}; // end of class FileSystem


/** The File class describes files inside the virtual filesystem. **/
class File : public Lockable {

	friend class FileSystem;

private:

	/** Unique identifier of this file within the filesystem. **/
	int32_t handle;

	/** Filesize in bytes. **/
	int32_t size;

	/** Current read/write position. **/
	int32_t seekPos;
	
	/** Size of each page in bytes. **/
	int32_t pageSize;

	/** Number of pages. Slots allocated for "pages". **/
	int32_t pageCount, allocatedCount;

	/** List of all pages that belong to this file. **/
	int32_t *pages;

	/** The filesystem that the file belongs to. **/
	FileSystem *fileSystem;

	/** Index number of the first page of this file. **/
	fs_pageno firstPage;

private:

	void init(FileSystem *fileSystem, fs_fileno fileHandle, bool create);

public:

	/** Does nothing, but we need it because of the derived classes. **/
	File();

	/**
	 * Creates a new file of initial size 0. If the file cannot be created, the
	 * "handle" attribute contains a negative value.
	 **/
	File(FileSystem *fileSystem);

	/**
	 * Opens a file that corresponds to the handle given by parameter "fileHandle".
	 * If "create" == true and the file exists, the file cannot be created. If
	 * "create" == false and the file does not exist, it cannot be opened. In general,
	 * if an error occurs, the "handle" attribute will contain a negative value.
	 **/
	File(FileSystem *fileSystem, fs_fileno fileHandle, bool create);

	/** Releases all resources occupied by the object (but does NOT delete the file). **/
	virtual ~File();

	/** Deletes the file from the filesystem it lives in. **/
	virtual void deleteFile();

	/** Returns the file handle of this file. If something went wrong, it is negative. **/
	virtual fs_fileno getHandle();

	/** Returns the size of the file or -1 if it does not exist. **/
	virtual off_t getSize();

	/** Returns the number of pages occupied. **/
	virtual int32_t getPageCount();

	/** Returns the current seek position. **/
	virtual off_t getSeekPos();

	/** Sets the seek position to the value given by parameter "newSeekPos". **/
	virtual int seek(off_t newSeekPos);

	/**
	 * Reads up to "bufferSize" bytes from the file, starting at the current seek
	 * position. Returns the number of bytes actually read from the file.
	 **/
	virtual int read(int bufferSize, void *buffer);

	/** Performs a seek and a read as one atomic operation. **/
	virtual int seekAndRead(off_t position, int bufferSize, void *buffer);

	/**
	 * Writes "bufferSize" bytes to the file, starting at the file's current seek
	 * position. Returns the number of bytes written to the file or -1 if something
	 * went wrong.
	 **/
	virtual int write(int bufferSize, void *buffer);

	/**
	 * Reads *bufferSize bytes from the file. Adjusts the value of *bufferSize to
	 * reflect the number of bytes actually read. *mustFreeBuffer tells the caller
	 * whether the buffer returned has to be freed.
	 **/
	virtual void *read(int *bufferSize, bool *mustFreeBuffer);

}; // end of class File


/**
 * FileSystemCacheSlot instances represent the filesystem pages that are stored
 * in the cache. The slots are organized as a queue (because queues are the
 * natural way to deal with LRU/FIFO).
 **/
typedef struct {
	/** Number of the filesystem page that is cached in this slot. **/
	int pageNumber;
	/** Indicates whether there have been write operations to this page. **/
	bool hasBeenChanged;
	/** The page data itself. **/
	char *data;
	/** Predecessor element in the cache queue. **/
	void *prev;
	/** Successor element in the cache queue. **/
	void *next;
} FileSystemCacheSlot;


/**
 * Instances of FileSystemCacheHashElement are used in the linked lists solving
 * the hash collisions.
 **/
typedef struct {
	/** Reference to the page stored in the cache. **/
	FileSystemCacheSlot *data;
	/** Next data element with same hash code. **/
	void *next;
} FileSystemCacheHashElement;


/** If we have a filesystem, we want to do caching. **/
class FileSystemCache {

	friend class FileSystem;

public:

	static const int DEFAULT_CACHE_SIZE = 128;

private:

	/** The filesystem we belong to. **/
	FileSystem *fileSystem;

	/** The size of each page (in bytes). **/
	int pageSize;

	/** The number of pages currently in the cache. **/
	int currentPageCount;

	/** Maximum number of pages in the cache. **/
	int cacheSize;

	/** FILESYSTEMCACHE_LRU or FILESYSTEMCACHE_FIFO. **/
	int workMode;

	/** The first page slot in the cache queue (sentinel). **/
	FileSystemCacheSlot *firstSlot;

	/** The last page slot in the cache queue (sentinel). **/
	FileSystemCacheSlot *lastSlot;

	FileSystemCacheHashElement **whereIsPage;

	/** Multi-purpose buffer for I/O operations. **/
	byte *readWriteBuffer;

private:

	/**
	 * Returns a reference to the slot containing the page specified by
	 * "pageNumber" or NULL if it is not in the cache.
	 **/
	FileSystemCacheSlot *findPage(int pageNumber);

	/** Moves the slot referenced by "slot" to the first position in the queue. **/
	void touchSlot(FileSystemCacheSlot *slot);

	/** Removes the entry for page "pageNumber" from the hashtable. **/
	void removeHashEntry(int pageNumber);

	/** For debugging only. **/
	void printCacheQueue();
	
public:

	/**
	 * Creates a new FileSystemCache instance. The cache can hold up to
	 * "pageCount" pages, each of size "pageSize". Data is read from and
	 * written to the file given by "dataFile".
	 **/
	FileSystemCache(FileSystem *fs, int pageSize, int pageCount);

	/** Deletes the object. **/
	~FileSystemCache();

	/** Returns true iff the page specified by "pageNumber" is in the cache. **/
	bool isInCache(int pageNumber);

	/** Sets the work mode of the cache to LRU or FIFO, depending on "newWorkMode". **/
	void setWorkMode(int newWorkMode);

	/**
	 * If the page requested is in the cache, it is copied to "buffer" and
	 * FILESYSTEM_SUCCESS is returned. Otherwise, FILESYSTEM_ERROR is returned.
	 **/
	int getPage(int pageNumber, void *buffer);

	/**
	 * Loads the page specified by "pageNumber" into the cache. Page data is read
	 * from "buffer". If "copyData" is true, then the data found in "buffer" are
	 * copied to the cache slot's own data storage. If "copyData" is false, then
	 * only the pointer is copied and the data belongs to the slot from now on.
	 **/
	FileSystemCacheSlot *loadPage(int pageNumber, void *buffer, bool copyData);

	/** Updates the timestamp of the page specified by "pageNumber". **/
	int touchPage(int pageNumber);

	/** Cached write operation. Returns FILESYSTEM_ERROR or FILESYSTEM_SUCCESS. **/
	int writeToPage(int pageNumber, int offset, int length, void *buffer);

	/** Cached read operation. Returns FILESYSTEM_ERROR or FILESYSTEM_SUCCESS. **/
	int readFromPage(int pageNumber, int offset, int length, void *buffer);

	/** Flushes the cache, i.e. writes all changes to disk. **/
	void flush();

private:

	/**
	 * Evicts the page in the slot specified by "slot" from the cache. If changes
	 * have been made to the page, data is written to disk before the eviction.
	 **/
	void evict(FileSystemCacheSlot *toEvict);

}; // end of class FileSystemCache

#endif



