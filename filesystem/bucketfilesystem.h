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
 * A BucketFileSystem can hold an arbitrary number of fixed-size files. Every
 * file has the same size.
 *
 * author: Stefan Buettcher
 * created: 2004-10-15
 * changed: 2004-11-10
 **/


#ifndef __FILESYSTEM__BUCKETFILESYSTEM_H
#define __FILESYSTEM__BUCKETFILESYSTEM_H


#include "filesystem.h"
#include "../misc/lockable.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>


typedef struct {
	bool changed;
	int bucket;
	int timeStamp;
	char *data;
} BucketCacheSlot;


class BucketFileSystem : public Lockable {

private:

	/**
	 * If we are loading a filesystem, we want to make sure it really is a filesystem.
	 * The first 32 bit of the file always have to be the value of FINGERPRINT.
	 **/
	static const int32_t FINGERPRINT = 912837123;

	static const double GROWTH_RATE = 1.31;

	static const int CACHE_SIZE = 4;

	BucketCacheSlot cache[CACHE_SIZE];

	/** Number of buckets and size of each bucket (in bytes). **/
	int32_t bucketSize, bucketCount;

	char *dataFileName;

	int dataFile, timeStamp;

public:

	/** Loads a virtual filesystem from the file given by parameter "fileName". **/
	BucketFileSystem(char *fileName);

	/**
	 * Creates a new virtual filesystem with uniform bucket size given by
	 * parameter "bucketSize" and initial number of buckets "bucketCount".
	 **/
	BucketFileSystem(char *fileName, int bucketSize, int bucketCount);

	/** Frees all resources occupied by the BucketFileSystem object. **/
	~BucketFileSystem();

	/** Returns the name of the data file. **/
	char *getFileName();

	/** Returns true iff the FileSystem instance represents an active filesystem. **/
	bool isActive();

	int readBucket(int bucket, char *data);

	int writeBucket(int bucket, char *data);

	/**
	 * Writes "count" bytes from "data" to the bucket with ID "bucket" at position
	 * "offset". Returns FILESYSTEM_SUCCESS or FILESYSTEM_ERROR.
	 **/
	int writeBucket(int bucket, char *data, int offset, int count);

	/** Returns the bucket size of the filesystem. **/
	int getBucketSize();

	/** Returns the number of buckets in the filesystem. **/
	int getBucketCount();

	/** Returns the size of the filesystem. **/
	off_t getSize();

private:

	/**
	 * Changes the size of the filesystem. The new bucket count has to be greater
	 * than the old value.#
	 **/
	int changeSize(int newBucketCount);

	/** This method is called before a bucket is evicted from the cache. **/
	void writeCacheSlot(int slot);

}; // end of class FileSystem


#endif



