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
 * The FileFile class is used to get read-only access to real files. FileFile
 * supports virtual files starting at a given offset within a real file. It can
 * also be used to read data from memory locations.
 *
 * author: Stefan Buettcher
 * created: 2005-01-09
 * changed: 2006-08-28
 **/


#ifndef __FILESYSTEM__FILEFILE_H
#define __FILESYSTEM__FILEFILE_H


#include "filesystem.h"


class FileFile : public File {

protected:

	/** Name of the file (if actual file). NULL otherwise. **/
	char *fileName;

	/** File that we are working on. **/
	int fileHandle;

	/** Start offset within the file. **/
	off_t startOffset;

	/** Current seek position, relative to startOffset. **/
	off_t seekPos;

	/** Number of objects using this FileFile instance. **/
	int usage;

	/** In case we are sitting on top of another FileFile. NULL otherwise. **/
	FileFile *underlying;

	/** In case we read our data from memory. **/
	char *buffer;

	/** Check, check. **/
	int bufferSize, bufferPos;

	/** Tells us whether the data buffer needs to be freed in the destructor or not. **/
	bool freeInDestructor;

	/**
	 * The value of GlobalVariables::forkCount when this object was created. We
	 * need to check before every file operation to make sure no fork has occurred
	 * underway. If a fork is detected, the file is reopened to avoid sharing the
	 * file descriptor between different processes.
	 **/
	int forkCountAtCreation;

public:

	/**
	 * Creates a new FileFile instance reading data from "buffer" (which contains
	 * "size" bytes of data). Depending on whether "copy" is true, the a copy of
	 * the buffer is created, or the object works on the original data (freeing it
	 * in the destructor).
	 **/
	FileFile(char *buffer, int size, bool copy, bool freeInDestructor);

	/**
	 * Creates a new file that starts at offset "startOffset" within the file given
	 * by "fileName". The "initialUsageCounter" is used to trigger automatic
	 * destruction of this object if used by other FileFile objects.
	 **/
	FileFile(const char *fileName, off_t startOffset = 0, int initialUsageCounter = 0);

	/** Creates a new file, using the given FileFile instance to do the actual I/O. **/
	FileFile(FileFile *file, off_t startOffset);

	/**
	 * Stupid destructor. If this FileFile instance uses another FileFile to perform
	 * the I/O, then the usage counter of that object is decreased. If it reaches 0,
	 * the object is deleted.
	 **/
	virtual ~FileFile();

	/** Crashes! **/
	virtual void deleteFile();

	/** Returns the internal file handle. **/
	virtual fs_fileno getHandle();

	/** Returns the filesize in bytes. **/
	virtual off_t getSize();

	/** Crashes! **/
	virtual int32_t getPageCount();

	/** Returns the current position of the read pointer. **/
	virtual off_t getSeekPos();

	/** Sets the position of the read pointer. **/
	virtual int seek(off_t newSeekPos);

	/** Reads up to "bufferSize" bytes into "buffer". **/
	virtual int read(int bufferSize, void *buffer);

	/** Crashes! **/
	virtual int write(int bufferSize, void *buffer);

	virtual void *read(int *bufferSize, bool *mustFreeBuffer);

private:

	/**
	 * To be called after a fork takes place. Closes the file handle (if open) and
	 * reopens the file so that no file descriptors are shared between processes.
	 **/
	void reopenFile();

}; // end of class FileFile


#endif


