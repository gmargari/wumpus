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
 * This file defines three basic data structures that are used within the
 * FileManager: IndexedDirectory, IndexedFile, and IndexedINode.
 *
 * author: Stefan Buettcher
 * created: 2005-02-17
 * changed: 2007-04-03
 **/


#ifndef __FILEMANAGER__DATA_STRUCTURES_H
#define __FILEMANAGER__DATA_STRUCTURES_H


#include "../index/index_types.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


/** This magical value is used to mark empty slots in the sorted array inside DirectoryContent. **/
#define DC_EMPTY_SLOT 984732861


typedef struct {

	/** Hash value of the slot, used for sorting. **/
	int32_t hashValue;

	/**
	 * ID of the object (file or directory) referred to. DC_EMPTY_SLOT if the
	 * slot does not contain anything (child has been removed).
	 **/
	int32_t id;

} DC_ChildSlot;


typedef struct DirectoryContent {

	/**
	 * Number of files and directories in the directory. This number may differ
	 * from (longAllocated + shortCount) in case of recent file deletions.
	 **/
	int32_t count;

	/** Number of IDs in the long list. **/
	int32_t longAllocated;

	/** The long list itself. **/
	DC_ChildSlot *longList;

	/** Number of children added but not yet merged into the sorted list. **/
	int16_t shortCount, shortSlotsAllocated;

	/** List of children added to the directory. **/
	DC_ChildSlot *shortList;

} DirectoryContent;


#define MAX_DIRECTORY_NAME_LENGTH (64 - 2 * sizeof(int32_t) - sizeof(void*) - 1)


/**
 * The IndexedDirectory data structure is used to represent the file system's
 * directory structure. Each directory has a unique ID and a parent directory.
 * For each directory, we maintain a balanced search tree that contains all
 * its children (directories and files). The nodes inside the tree are sorted
 * by the hash value of their name.
 **/
typedef struct {

	/** Unique ID of this directory. **/
	int32_t id;

	/**
	 * ID of the parent directory. "parent == id" means there is no parent, and
	 * we are the root directory.
	 **/
	int32_t parent;

	/** Owner of this directory. **/
	uid_t owner;

	/** User group associated with this directory. **/
	gid_t group;

	/** Unix-style directory permissions. **/
	mode_t permissions;

	/**
	 * Name of the directory. We do not support file search inside directories
	 * that have names longer than ~50 characters. If the directory is a mount
	 * point, the name starts with "/dev/".
	 **/
	char name[MAX_DIRECTORY_NAME_LENGTH + 1];

	/** For faster access, we store the name's hash value here. **/
	int32_t hashValue;

	/**
	 * Container structure for all children (files and directories) of this
	 * directory.
	 **/
	DirectoryContent children;

} IndexedDirectory;


#define MAX_FILE_NAME_LENGTH (64 - 2 * sizeof(int32_t) - 1)

/**
 * The IndexedFile data structure is used to maintain file information in
 * memory. An IndexedFile instance represents a file system hard link. A hard
 * link is a pointer to an INode.
 **/
typedef struct {

	/** Unique ID of the INode that this file refers to. **/
	int32_t iNode;

	/** ID of the parent directory which we reside in. **/
	int32_t parent;

	/** Hash value of the file's name (used to speed up search operations). **/
	int32_t hashValue;

} IndexedFile;


/**
 * The VisibleExtent structure is used to obtain a list of all visible extents
 * for a certain user from the FileManager.
 **/
typedef struct {

	/** ID of the file that this object belongs to. **/
	int32_t fileID;

	/** Start offset inside the index. **/
	offset startOffset;

	/** Number of tokens inside the file. **/
	uint32_t tokenCount;

	/** File type. **/
	int16_t documentType;

} VisibleExtent;


/**
 * This data structure is used to maintain file information on disk. For every
 * file, there is a 48-byte sequence of data, containing the file ID, INode ID,
 * parent directory, and file name.
 **/
typedef struct {

	int32_t iNode;

	int32_t parent;

	char fileName[MAX_FILE_NAME_LENGTH + 1];

} IndexedFileOnDisk;


/**
 * Instances of IndexedINode keep all information about the INodes found in the
 * file system that we need to process search queries.
 **/
typedef struct {

	/**
	 * We have a hash table that contains all INodes in the system. Collisions
	 * are solved by linked lists. This is the pointer to the next element in
	 * the list.
	 **/
	int32_t nextINode;

	/** Device-specific unique INode ID. **/
	ino_t iNodeID;

	/** How many hard links does this INode have? **/
	uint16_t hardLinkCount;

	/** User account owning this INode. **/
	uint16_t owner;

	/** User group associated with this INode. **/
	uint16_t group;

	/** Unix-style file permissions. **/
	uint16_t permissions;

	/** Index position at which the INode starts. **/
	offset startInIndex;

	/**
	 * Number of tokens indexed. We do not index files that contain more than
	 * 4 billion tokens.
	 **/
	uint32_t tokenCount;

	/** One of the values defined in "inputfilter.h". **/
	int16_t documentType;

	/** One of the values defined in "stemmer.h". **/
	int16_t language;

} IndexedINode;


/**
 * Persistent INode information storage is realized by a sequence of
 * IndexedINodeOnDisk instances. They are stored inside the file "index.inodes"
 * and maintained by the FileManager.
 **/
typedef struct {

	/** This is the data we need to maintain the in-memory INode list. **/
	IndexedINode coreData;

	/** File size on disk. **/
	off_t fileSize;

	/**
	 * When was this file indexed the last time? This value is the number of
	 * seconds since 00:00:00 UTC, January 1, 1970.
	 **/
	time_t timeStamp;

	/**
	 * Address space reserved for this file. Only used if we support append
	 * operations with indexing-time transformations.
	 **/
	uint32_t reservedTokenCount;

} IndexedINodeOnDisk;


#endif


