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
 * author: Stefan Buettcher
 * created: 2005-01-09
 * changed: 2006-09-19
 **/


#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "filefile.h"
#include "../misc/all.h"


static const char *LOG_ID = "FileFile";


FileFile::FileFile(char *buffer, int size, bool copy, bool freeInDestructor) {
	assert(buffer != NULL);
	fileHandle = -1;
	this->fileName = NULL;
	underlying = NULL;
	usage = 0;
	if (copy) {
		this->buffer = (char*)malloc(size);
		memcpy(this->buffer, buffer, size);
	}
	else
		this->buffer = buffer;
	bufferSize = size;
	bufferPos = 0;
	this->freeInDestructor = freeInDestructor;
} // end of FileFile(char*, int, bool)


FileFile::FileFile(const char *fileName, off_t startOffset, int initialUsageCounter) {
	forkCountAtCreation = GlobalVariables::forkCount;
	this->fileName = duplicateString(fileName);
	fileHandle = open(fileName, O_RDONLY | O_SYNC | O_LARGEFILE);
	if (fileHandle >= 0) {
		off_t fileSize = lseek(fileHandle, (off_t)0, SEEK_END);
		if (startOffset > fileSize)
			startOffset = fileSize;
		this->startOffset = startOffset;
		lseek(fileHandle, (off_t)0, SEEK_SET);
		seekPos = 0;
	}
	else {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to open file: \"%s\". errno = %d.", fileName, errno);
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
	usage = initialUsageCounter;
	underlying = NULL;
	buffer = NULL;
} // end of FileFile(char*, off_t, int)


FileFile::FileFile(FileFile *file, off_t startOffset) {
	this->fileName = NULL;
	this->fileHandle = -1;
	this->startOffset = startOffset;
	seekPos = 0;
	usage = 0;
	underlying = file;
	underlying->usage++;
	buffer = NULL;
} // end of FileFile(FileFile*, off_t)


FileFile::~FileFile() {
	if (fileHandle >= 0) {
		close(fileHandle);
		fileHandle = -1;
	}
	if (fileName != NULL) {
		free(fileName);
		fileName = NULL;
	}
	if (underlying != NULL) {
		if (--underlying->usage == 0)
			delete underlying;
		underlying = NULL;
	}
	if ((buffer != NULL) && (freeInDestructor)) {
		free(buffer);
		buffer = NULL;
	}
} // end of ~FileFile()


void FileFile::deleteFile() {
	assert("Operation not supported" == NULL);
}


fs_fileno FileFile::getHandle() {
	return fileHandle;
}


off_t FileFile::getSize() {
	if (underlying != NULL)
		return underlying->getSize() - startOffset;
	else if (buffer != NULL)
		return bufferSize;
	else if (fileHandle < 0)
		return 0;
	else {
		LocalLock lock(this);
		if (forkCountAtCreation != GlobalVariables::forkCount)
			reopenFile();
		off_t oldPosition = lseek(fileHandle, (off_t)0, SEEK_CUR);
		off_t result = lseek(fileHandle, (off_t)0, SEEK_END);
		lseek(fileHandle, oldPosition, SEEK_SET);
		return result - startOffset;
	}
} // end of getSize()


int32_t FileFile::getPageCount() {
	return 0;
} // end of getPageCount()


off_t FileFile::getSeekPos() {
	if (buffer != NULL)
		return bufferPos;
	if (fileHandle < 0)
		return 0;
	return seekPos;
} // end of getSeekPos()


int FileFile::seek(off_t newSeekPos) {
	if (buffer != NULL) {
		if (newSeekPos <= 0)
			bufferPos = 0;
		else if (newSeekPos >= bufferSize)
			bufferPos = bufferSize;
		else
			bufferPos = newSeekPos;
		return FILESYSTEM_SUCCESS;
	}
	else if (newSeekPos < 0)
		return FILESYSTEM_ERROR;
	else if (underlying != NULL) {
		seekPos = newSeekPos;
		return FILESYSTEM_SUCCESS;
	}
	else {
		LocalLock lock(this);
		if (forkCountAtCreation != GlobalVariables::forkCount)
			reopenFile();
		if ((seekPos = lseek(fileHandle, newSeekPos + startOffset, SEEK_SET)) < 0) {
			seekPos = 0;
			return FILESYSTEM_ERROR;
		}
		else
			return FILESYSTEM_SUCCESS;
	}
} // end of seek(off_t)


int FileFile::read(int bufferSize, void *buffer) {
	LocalLock lock(this);
	int result = 0;

	if (underlying != NULL) {
		LocalLock lock2(underlying);
		off_t oldSeekPos = underlying->getSeekPos();
		if (underlying->seek(seekPos + startOffset) != FILESYSTEM_SUCCESS)
			result = 0;
		else {
			result = underlying->read(bufferSize, buffer);
			if (result > 0)
				seekPos += result;
		}
		underlying->seek(oldSeekPos);
	}
	else {
		if (this->buffer != NULL) {
			if (bufferPos + bufferSize > this->bufferSize)
				bufferSize = (this->bufferSize - bufferPos);
			memcpy(buffer, &this->buffer[bufferPos], bufferSize);
			bufferPos += bufferSize;
			result = bufferSize;
		}
		else {
			if (forkCountAtCreation != GlobalVariables::forkCount)
				reopenFile();
			result = forced_read(fileHandle, buffer, bufferSize);
			if (result > 0)
				seekPos += result;
		}
	}

	return (result < 0 ? 0 : result);
} // end of read(int, void*)


int FileFile::write(int bufferSize, void *buffer) {
	LocalLock lock(this);
	int result = 0;

	if (underlying != NULL) {
		LocalLock lock2(underlying);
		off_t oldSeekPos = underlying->getSeekPos();
		if (underlying->seek(seekPos + startOffset) != FILESYSTEM_SUCCESS)
			result = 0;
		else {
			result = underlying->write(bufferSize, buffer);
			if (result > 0)
				seekPos += result;
		}
		underlying->seek(oldSeekPos);
	}
	else {
		if (this->buffer != NULL) {
			if (bufferPos + bufferSize > this->bufferSize)
				bufferSize = (this->bufferSize - bufferPos);
			memcpy(&this->buffer[bufferPos], buffer, bufferSize);
			bufferPos += bufferSize;
			result = bufferSize;
		}
		else {
			assert("Operation not supported" == NULL);
		}
	}

	return (result < 0 ? 0 : result);
} // end of write(int, void*)


void * FileFile::read(int *bufferSize, bool *mustFreeBuffer) {
	LocalLock lock(this);
	void *result;

	if (underlying != NULL) {
		LocalLock lock2(underlying);
		off_t oldSeekPos = underlying->getSeekPos();
		if (underlying->seek(seekPos + startOffset) != FILESYSTEM_SUCCESS) {
			result = NULL;
			*bufferSize = 0;
			*mustFreeBuffer = false;
		}
		else {
			result = underlying->read(bufferSize, mustFreeBuffer);
			if (*bufferSize > 0)
				seekPos += *bufferSize;
		}
		underlying->seek(oldSeekPos);
	}
	else {
		if (this->buffer != NULL) {
			if (bufferPos + *bufferSize > this->bufferSize)
				*bufferSize = (this->bufferSize - bufferPos);
			*mustFreeBuffer = false;
			result = &this->buffer[bufferPos];
			if (*bufferSize > 0)
				bufferPos += *bufferSize;
		}
		else {
			if (forkCountAtCreation != GlobalVariables::forkCount)
				reopenFile();
			result = malloc(*bufferSize + 1);
			*bufferSize = read(*bufferSize, result);
			*mustFreeBuffer = true;
		}
	}

	return result;
} // end of read(int*, bool*)


void FileFile::reopenFile() {
	assert(fileHandle >= 0);
	close(fileHandle);
	forkCountAtCreation = GlobalVariables::forkCount;
	fileHandle = open(fileName, O_RDONLY | O_SYNC | O_LARGEFILE);
	if (fileHandle < 0) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to reopen file after fork: %s", fileName);
		fileHandle = -1;
	}
	off_t status = lseek(fileHandle, seekPos, SEEK_SET);
	if (status == (off_t)-1) {
		snprintf(errorMessage, sizeof(errorMessage),
				"Unable to seek properly after reopening file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
} // end of reopenFile()


