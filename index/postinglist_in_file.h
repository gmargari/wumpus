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
 * The PostingListInFile class is responsible for maintaining a file that
 * contains a single, compressed posting list. PostingListInFile is used when
 * hybrid index maintenance is selected, in which case all posting lists that
 * are longer than a certain threshold are stored in separate files.
 *
 * The general structure of the file is (from back to front):
 *
 *   1 x PostingListFileHeader
 *   M x PostingListSegmentHeader
 *   N x PostingListSegment
 *
 * This is actually very similar to the file format used by CompactIndex, which
 * is important if I want to return postings by SegmentedPostingList instances.
 *
 * author: Stefan Buettcher
 * created: 2005-08-05
 * changed: 2007-02-12
 **/


#ifndef __INDEX__POSTINGLIST_IN_FILE_H
#define __INDEX__POSTINGLIST_IN_FILE_H


#include "compactindex.h"
#include "index_types.h"
#include "../extentlist/extentlist.h"


class PostingListInFile {

private:

	/** Copy of the file name. **/
	char *fileName;

	/** Handle to the data file. **/
	int fileHandle;

	/** Total number of postings. **/
	int64_t postingCount;

	/** Total number of segments. **/
	int32_t segmentCount;

	/** Total size of the file (in bytes, with and without headers). **/
	off_t fileSize, fileSizeWithoutHeaders;

	/** List of segment headers (with pre-allocation). **/
	PostingListSegmentHeader *segmentHeaders;

	/** Number of segment headers allocated in memory. **/
	int32_t segmentsAllocated;

	/** Is the last segment currently in memory or on disk? **/
	bool lastSegmentIsInMemory;

	/** Uncompressed postings data for last segment. **/
	offset *lastSegment;

	/**
	 * Tells us whether this list has been modified (if so, we have to save
	 * the new header data to disk in the destructor).
	 **/
	bool modified;

public:

	/**
	 * Creates a new PostingListInFile instance associated with the posting list
	 * found in the given file. If the file does not exist, it will be created.
	 **/
	PostingListInFile(char *file);

	/** Saves changes to the data file and releases all resources. **/
	~PostingListInFile();

	/** Adds a sequence of "count" postings to the posting list. **/
	void addPostings(offset *postings, int count);

	/** Same as above, but for compressed postings. **/
	void addPostings(byte *compressed, int byteSize, int count, offset first, offset last);

	/**
	 * Returns a PostingList or SegmentedPostingList instance representing the
	 * postings currently found inside the file.
	 **/
	ExtentList *getPostings(int memoryLimit);

	int64_t getPostingCount() { return postingCount; }

	int64_t getFileSize() { return fileSize; }

	/**
	 * Returns a pointer to the name of this object's data file. Do not mess with it!
	 * Do not free!
	 **/
	char *getFileName() { return fileName; }

private:

	void loadLastSegmentIntoMemory();

	/** Adds the given sequence of postings to the last segment (which is in memory). **/
	void addToLastSegment(offset *postings, int count);

	void writeLastSegmentToDisk();

	/** Adds a new segment descriptor for an empty segment. **/
	void addEmptySegment();

	/** Adds a new segment, containing the given data. **/
	void addNewSegment(offset *postings, int count);

	/** Same as above, but for compressed postings. **/
	void addNewSegment(byte *postings, int byteSize, int count, offset first, offset last);

}; // end of class PostingListInFile


#endif



