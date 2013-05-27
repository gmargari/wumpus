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
 * Definition of the IndexToText class. IndexToText is used to translate index
 * offsets to file offsets in the original files that have been indexed.
 *
 * author: Stefan Buettcher
 * created: 2005-03-08
 * changed: 2007-12-17
 **/


#ifndef __INDEX__INDEXTOTEXT__H
#define __INDEX__INDEXTOTEXT__H


#include "index_types.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"


/**
 * InMemoryMapping instances are used to index into the on-disk mappings
 * we store inside the data file.
 **/
typedef struct {

	/**
	 * This is the position of this piece of data within the index->text map.
	 * We try to keep a distance of IndexToText::INDEX_GRANULARITY between
	 * two subsequence InMemoryMapping instances.
	 **/
	offset positionInMapping;
	
	/** What index position does this piece of information refer to? **/
	offset indexPosition;

	/** Number of OnDiskMapping objects controlled by this InMemoryMapping. **/
	int32_t chunkSize;

} InMemoryMapping;


/**
 * This structure describes an indexPosition/filePosition pair stored on disk.
 * We try top keep an index distance of 2000 between the pairs. This guarantees
 * that we do not have to store more than 0.008 bytes per posting, which keeps
 * the map acceptably small.
 **/
typedef struct {

	/** Index position of the token referred to. **/
	offset indexPosition;

	/** In-file position of the token referred to. **/
	off_t filePosition;

} OnDiskMapping;


class IndexToText : public Lockable {

private:

	/** Number of on-disk entries per in-memory entry. **/
	static const int INDEX_GRANULARITY = 2048;

	/** File that contains all the IndexToText data. **/
	char *fileName;

	/** Handle to the data file. **/
	int fileHandle;

	/** Are we in read-only mode? **/
	bool readOnly;

	/** This is the number of mappings we have stored in the data file. **/
	int64_t numberOfMappingsOnDisk;

	/**
	 * We keep an index into the on-disk data in memory. This is the number of
	 * mappings we have in memory.
	 **/
	int64_t numberOfMappingsInMemory;

	/**
	 * Number of in-memory slots allocated. This is usually
	 * (numberOfMappingsInMemory & ~1024) + 1024.
	 **/
	int memorySlotsAllocated;

	/** An array containing all in-memory mappings. **/
	InMemoryMapping *inMemoryMappings;

public:

	/**
	 * Creates a new IndexToText instance. If "create == true", a new data file
	 * will be created on disk. If "create == false", data will be loaded from
	 * an existing on-disk representation.
	 **/
	IndexToText(const char *workDirectory, bool create);

	/** Creates a fresh IndexToText instance. A new file ("fileName") is created. **/
	IndexToText(const char *fileName);

	/** Class destructor. **/
	~IndexToText();

	/** Writes all pending data to disk. **/
	void saveToDisk();

	/**
	 * Adds a single mapping to the list of mappings. Since "tpp" only contains a
	 * 32-bit file-specific index position, we give "fileStart" in addition so that
	 * the IndexToText instance can translate to index-wide value.
	 **/
	void addMapping(offset fileStart, TokenPositionPair tpp);

	/** Adds a sequence of mappings (non-decreasing) of length "count" to the list. **/
	void addMappings(offset fileStart, TokenPositionPair *tpp, int count);

	/**
	 * Asks for the biggest indexPosition/filePosition pair such that the indexPosition
	 * is smaller than or equal to the one given by "where". Returns true if such a
	 * pair can be found, false otherwise.
	 **/
	bool getLastSmallerEq(offset where, offset *indexPosition, off_t *filePosition);

	/**
	 * Removes all indexPosition/filePosition pairs from the IndexToText instance
	 * unless they lie in one of the intervals given by "files".
	 **/
	void filterAgainstFileList(ExtentList *files);
	
}; // end of class IndexToText


#endif


