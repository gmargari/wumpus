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
 * Definition of the DocumentCache class. DocumentClass is used to speed
 * up retrieving the text from documents for which parsing is very time-consuming,
 * such as PostScript, PDF, and Office documents. Speedup is achieved by keeping
 * a raw text version of these documents in an on-disk cache.
 *
 * Data in the cache are managed on a priority basis. The cache priority of a
 * documents is a combination of the time it takes to do the plaintext conversion
 * and the time at which the document was last accessed.
 *
 * author: Stefan Buettcher
 * created: 2005-06-30
 * changed: 2009-02-01
 **/


#ifndef __INDEXCACHE__DOCUMENTCACHE_H
#define __INDEXCACHE__DOCUMENTCACHE_H


#include "../filters/inputstream.h"
#include "../index/index_types.h"
#include "../misc/lockable.h"


typedef struct {

	/** Unique ID of the iNode that contains this document. **/
	ino_t iNodeID;

	/** Compressed size (in bytes) of this document. **/
	int32_t compressedSize;

	/** How long did it take (in ms) to extract the text from the file? **/
	int32_t conversionTime;

	/** Used by the LRU cache strategy. **/
	time_t timeStamp;

	/** To resolve collisions in "timeStamp". **/
	int64_t timeStamp2;

	/** Unique, random file name (refers to the file in the cache directory). **/
	char fileName[8];

} CachedDocumentDescriptor;


class DocumentCache : public Lockable {

public:

	/**
	 * We refuse to cache documents whose plain-text representation is larger
	 * than this (size in bytes).
	 **/
	static const int MAX_CACHEABLE_FILESIZE = 1024 * 1024;

	/**
	 * We refuse to cache documents that can be converted to plain text in less
	 * than this many milliseconds.
	 **/
	static const int MIN_CONVERSION_TIME = 10;

	/**
	 * Tells us for every file format supported by the input filters whether files
	 * of that format can be put into the document cache.
	 **/
	bool isCacheable[FilteredInputStream::MAX_DOCUMENT_TYPE + 1];

protected:

	CachedDocumentDescriptor *descriptors;

	/** Maximum number of files to be held in the cache at a time. **/
	int64_t maxFileCount;

	/** Maximum total size (compressed, in bytes) of all files in the cache. **/
	int64_t maxTotalSize;

	/** Number of files in the cache. **/
	int64_t fileCount;

	/** Total size (compressed) of all files in the cache. **/
	int64_t totalSize;

	/** Counter for LRU. **/
	int64_t currentTimeStamp;

	/** Directory where we keep all the files. **/
	char *directory;

public:

	/**
	 * Creates a new DocumentCache instance with data stored in the given
	 * directory. If the directory does not exist, or no cache metadata can be
	 * found in the directory, a fresh on-disk cache is created.
	 **/
	DocumentCache(char *directory);

	/** Deletes the object, but leaves all on-disk cache data intact. **/
	~DocumentCache();

	/**
	 * Adds the given document ("filePath") to the cache. The document type of
	 * the file is given by "documentType".
	 **/
	void addDocumentTextFromFile(char *filePath, int documentType);

	/**
	 * Adds the given document ("filePath") to the document cache. The plain text
	 * of the file is taken from "plainTextFile". The time to convert the original
	 * file to the text version is given by "conversionTime" (in ms). The conversion
	 * time is used to assign a priority value to the document.
	 **/
	void addDocumentTextFromFile(
			char *filePath, char *plainTextFile, int conversionTime);

	/**
	 * Similar to addDocumentTextFromFile, but data is given in a char buffer
	 * referenced by "plainText".
	 **/
	void addDocumentTextFromMemory(
			char *filePath, char *plainText, int length, int conversionTime);

	/**
	 * Returns the text corresponding to the given file ("filePath") or NULL if
	 * the file is not in the cache. Sets the length of the outpuf buffer. Memory
	 * has to be freed by the caller.
	 **/
	char *getDocumentText(char *filePath, int *size);

	/** Same as above, but with iNode ID instead of file name. **/
	char *getDocumentText(ino_t iNode, int *size);

	/** Does the obvious. **/
	static char *compressBuffer(const char *buffer, int inputSize, int *outputSize);

	/** Does the obvious. **/
	static char *decompressBuffer(const char *buffer, int inputSize, int *outputSize);

private:

	/**
	 * Returns the ID of a free cache slot that can be used to store a new document
	 * of size "size" bytes. -1 if no free slot can be found. The parameter "mayEvict" 
	 * is used to tell the cache whether it is allowed to remove documents in order
	 * to make room for a new one.
	 **/
	int findFreeCacheSlot(bool mayEvict, int size);

	/** Evicts the document with worst LRU score from the cache. **/
	void evictDocument();

	/** Evicts the document with ID value "id" from the cache. **/
	void evict(int id);

	/**
	 * Creates a random file name of length "length" such that there is no file
	 * inside the cache directory that has the same name.
	 **/
	void randomFileName(char *buffer, int length);

	/** Saves the cache meta-information ("cache.dat") to disk. **/
	void saveToDisk();

	/** Counterpart to saveToDisk(). **/
	void loadFromDisk();

	void saveToFile(char *fileName, char *buffer, int size);

	/**
	 * Returns a buffer containing data read from the given file descriptor. Sets
	 * the value of *size to the number of bytes actually read. Memory has to be
	 * freed by the caller.
	 **/
	static char *fillBufferFromFile(int fd, int *size);

	/** Counterpart to fillBufferFromFile. **/
	static void fillFileFromBuffer(int fd, const char *buffer, int size);

	/** Some stuff. **/
	static char *compressDecompress(const char *param, const char *buffer, int inputSize, int *outputSize);

}; // end of class DocumentCache


#endif


