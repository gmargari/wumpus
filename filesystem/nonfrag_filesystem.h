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
 * This file contains the class definition of the NonFragFileSystem class.
 * NonFragFileSystem implements a filesystem-within-a-file whose internal
 * files cannot have any fragmentation. This is achieved by having the files
 * say upon creation how many pages they will occupy. When an existing file
 * has to be extended, the only way to do this is to create a new file and
 * copy the old data.
 *
 * author: Stefan Buettcher
 * created: 2004-10-19
 * changed: 2004-10-21
 **/


#ifndef __FILESYSTEM__NONFRAG_FILESYSTEM_H
#define __FILESYSTEM__NONFRAG_FILESYSTEM_H


#include "filesystem.h"
#include "pagecomparators.h"
#include "../misc/general_avltree.h"


class NonFragFileSystem {

public:

	static const int MIN_PAGE_COUNT = 256;
	static const int DEFAULT_PAGE_COUNT = 1024;

	static const int MIN_PAGE_SIZE = 256;
	static const int DEFAULT_PAGE_SIZE = 1024;

	static const int FILESYSTEM_ACCESS = O_RDWR;
	static const int FILESYSTEM_FINGERPRINT = 876282111;

	static const int COPYBUFFER_SIZE = 65536;

private:

	// We are keeping track of free space within the filesystem using to search
	// trees: The first contains the list of free page intervals, sorted by position,
	// the second sorted by size.

	/** List of free page intervals, sorted by position. **/
	GeneralAVLTree *freeSpaceSortedByPosition;

	/** List of free page intervals, sorted by size. **/
	GeneralAVLTree *freeSpaceSortedBySize;
	
	/** Used to compare two page intervals by position. **/
	Comparator *positionComparator;

	/** Used to compare two page intervals by size. **/
	Comparator *sizeComparator;

	/** Number of pages in the filesystem. **/
	int pageCount;

	/** Size of each page in bytes. **/
	int pageSize;

	/** Number of files in the filesystem. **/
	int fileCount;

	/** Number of slots allocated in memory. **/
	int fileSlotCount;

	/** Number of free fileID slots. **/
	int freeSlotCount;

	/** List of free slots. **/
	int32_t *freeSlots;

	/**
	 * Tells us for every file what its first page is. If the file does not exist,
	 * "firstPageOfFile" contains a value of -1 at that position.
	 **/
	int32_t *firstPageOfFile;

	/**
	 * Number of pages consumed by each file. If the file does not exist,
	 * "filePageCount" contains a value of -1 at that position.
	 **/
	int32_t *filePageCount;

	/** Handle to the filesystem's file. **/
	int fileHandle;

public:

	/** Creates a NonFragFileSystem instance from existing data on disk. **/
	NonFragFileSystem(char *fileName);

	/** Creates a new NonFragFileSystem with given parameters. **/
	NonFragFileSystem(char *fileName, int pageSize, int pageCount);

	/** Deletes the object, frees the memory, closes the file. **/
	~NonFragFileSystem();

	/**
	 * Creates a new file with a size of "fileSize" bytes. Returns the file ID of
	 * the new file. If the file could not be created, FILESYSTEM_ERROR is returned.
	 **/
	int createFile(int fileSize);

	/**
	 * Deletes the file with ID "fileID". Returns either FILESYSTEM_SUCCESS or
	 * FILESYSTEM_ERROR.
	 **/
	int deleteFile(int fileID);

	/**
	 * Tries to read "length" bytes from file "fileID", position "off", into
	 * the buffer referenced by "buffer". Returns the number of bytes actually
	 * read or FILESYSTEM_ERROR if an error occured.
	 **/
	int readFile(int fileID, char *buffer, int off, int length);

	/**
	 * Tries to write "length" bytes stored in the buffer referenced by "buffer"
	 * to file "fileID", position "off". Returns the number of bytes actually
	 * written or FILESYSTEM_ERROR if an error occured.
	 **/
	int writeFile(int fileID, char *buffer, int off, int length);

	/**
	 * Returns the size of the file given by "fileID" in bytes or -1 if no such
	 * file exists.
	 **/
	int getFileSize(int fileID);

	/**
	 * Copies data from one file to another. The size of the data to be copied is
	 * given by "length". Returns FILESYSTEM_SUCCESS or FILESYSTEM_ERROR, depending
	 * on what happened.
	 **/
	int copyFile(int file1, int off1, int file2, int off2, int length);

	/**
	 * Returns the index of the first page of the file specified by "fileID" or
	 * -1 if the file does not exist.
	 **/
	int getFirstPage(int fileID);

private:

	/**
	 * Marks the interval starting at "start" of length "length" as marked by
	 * modifying both search trees.
	 **/
	void markAsOccupied(int start, int length);

	/** Counterpart to markAsOccupied. **/
	void markAsFree(int start, int length);

	/** Writes an int value to the filesystem. Increases the file pointer. **/
	void writeInt(int value);

	/** Reads an int value from the filesystem. Increases the file pointer. **/
	int readInt();

	/** Increases the number of pages in the filesystem to "newPageCount". **/
	void increasePageCount(int newPageCount);

}; // end of class NonFragFileSystem


#endif


