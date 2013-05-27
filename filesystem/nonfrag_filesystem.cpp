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
 * Implementation of the class NonFragFileSystem. See the header file for details.
 *
 * author: Stefan Buettcher
 * created: 2004-10-20
 * changed: 2004-10-21
 **/


#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "nonfrag_filesystem.h"
#include "../misc/alloc.h"


NonFragFileSystem::NonFragFileSystem(char *fileName) {
	fileHandle = open(fileName, FILESYSTEM_ACCESS);
	if (fileHandle >= 0) {
		lseek(fileHandle, 0, SEEK_SET);
		int fingerPrint = readInt();
		if (fingerPrint != FILESYSTEM_FINGERPRINT) {
			close(fileHandle);
			fileHandle = -1;
			return;
		}
		pageCount = readInt();
		pageSize = readInt();
		fileCount = readInt();
		fileSlotCount = readInt();
		positionComparator = new PageIntervalPositionComparator();
		freeSpaceSortedByPosition = new GeneralAVLTree(positionComparator);
		sizeComparator = new PageIntervalSizeComparator();
		freeSpaceSortedBySize = new GeneralAVLTree(sizeComparator);
		firstPageOfFile = (int32_t*)malloc((fileSlotCount + 1) * sizeof(int32_t));
		filePageCount = (int32_t*)malloc((fileSlotCount + 1) * sizeof(int32_t));
		off_t endOfFileSystem = pageCount;
		endOfFileSystem *= pageSize;
		lseek(fileHandle, endOfFileSystem, SEEK_SET);
		forced_read(fileHandle, firstPageOfFile, fileSlotCount * sizeof(int32_t));
		forced_read(fileHandle, filePageCount, fileSlotCount * sizeof(int32_t));
		PageInterval *interval = (PageInterval*)malloc(sizeof(PageInterval));
		interval->start = 1;
		interval->length = pageCount - 1;
		freeSpaceSortedByPosition->insertNode(interval);
		freeSpaceSortedBySize->insertNode(interval);
		freeSlotCount = fileSlotCount - fileCount;
		freeSlots = (int32_t*)malloc(freeSlotCount * sizeof(int32_t));
		freeSlotCount = 0;
		for (int i = 0; i < fileSlotCount; i++)
			if (firstPageOfFile[i] >= 0)
				markAsOccupied(firstPageOfFile[i], filePageCount[i]);
			else
				freeSlots[freeSlotCount++] = i;
	}
} // end of NonFragFileSystem(char*)


NonFragFileSystem::NonFragFileSystem(char *fileName, int pageSize, int pageCount) {
	fileHandle = open(fileName, FILESYSTEM_ACCESS);
	if (fileHandle >= 0) {
		this->pageSize = pageSize;
		this->pageCount = pageCount;
		positionComparator = new PageIntervalPositionComparator();
		freeSpaceSortedByPosition = new GeneralAVLTree(positionComparator);
		sizeComparator = new PageIntervalSizeComparator();
		freeSpaceSortedBySize = new GeneralAVLTree(sizeComparator);
		fileCount = 0;
		fileSlotCount = 1024;
		firstPageOfFile = (int32_t*)malloc((fileSlotCount + 1) * sizeof(int32_t));
		filePageCount = (int32_t*)malloc((fileSlotCount + 1) * sizeof(int32_t));
		freeSlots = (int32_t*)malloc(fileSlotCount * sizeof(int32_t));
		freeSlotCount = fileSlotCount;
		for (int i = 0; i < fileSlotCount; i++) {
			firstPageOfFile[i] = filePageCount[i] = -1;
			freeSlots[i] = i;
		}
		PageInterval *interval = (PageInterval*)malloc(sizeof(PageInterval));
		interval->start = 1;
		interval->length = pageCount - 1;
		freeSpaceSortedByPosition->insertNode(interval);
		freeSpaceSortedBySize->insertNode(interval);
	}
} // end of NonFragFileSystem(char*, int, int)


NonFragFileSystem::~NonFragFileSystem() {
	if (fileHandle >= 0) {
		// write organizational data and close file
		lseek(fileHandle, 0, SEEK_SET);
		writeInt(FILESYSTEM_FINGERPRINT);
		writeInt(pageCount);
		writeInt(pageSize);
		writeInt(fileCount);
		writeInt(fileSlotCount);
		off_t endOfFileSystem = pageCount;
		endOfFileSystem *= pageSize;
		lseek(fileHandle, endOfFileSystem, SEEK_SET);
		forced_write(fileHandle, firstPageOfFile, fileSlotCount * sizeof(int32_t));
		forced_write(fileHandle, filePageCount, fileSlotCount * sizeof(int32_t));
		close(fileHandle);
		fileHandle = -1;

		// free all memory occupied
		GeneralAVLTreeNode *node = freeSpaceSortedByPosition->getLeftMost();
		while (node != NULL) {
			if (node->value != NULL)
				free(node->value);
			node = freeSpaceSortedByPosition->getNext(node);
		}
		free(firstPageOfFile);
		free(filePageCount);
		free(freeSlots);
		delete freeSpaceSortedByPosition;
		delete positionComparator;
		delete freeSpaceSortedBySize;
		delete sizeComparator;
	}
} // end of ~NonFragFileSystem()


int NonFragFileSystem::createFile(int fileSize) {

	// set "fileSize" value to full page size
	int modulo = fileSize % pageSize;
	if (modulo != 0)
		fileSize += (pageSize - modulo);

	// look for a place where we can put the file
	PageInterval lookiLooki;
	lookiLooki.start = 0;
	lookiLooki.length = fileSize / pageSize;
	GeneralAVLTreeNode *freeNode =
		freeSpaceSortedBySize->findSmallestBiggerEq(&lookiLooki);

	// if no such place can be found, we have to increase the filesystem
	if (freeNode == NULL) {
		int value1 = pageCount + 2 * lookiLooki.length;
		int value2 = (int)(pageCount * 1.31);
		if (value1 > value2)
			increasePageCount(value1);
		else
			increasePageCount(value2);
		freeNode = freeSpaceSortedBySize->findSmallestBiggerEq(&lookiLooki);
	}

	// look if we have free file IDs left; if not, increase the array
	if (freeSlotCount <= 0) {
		free(freeSlots);
		firstPageOfFile = (int32_t*)realloc(firstPageOfFile, fileSlotCount * 2 * sizeof(int32_t));
		filePageCount = (int32_t*)realloc(filePageCount, fileSlotCount * 2 * sizeof(int32_t));
		for (int i = fileSlotCount; i < 2 * fileSlotCount; i++)
			firstPageOfFile[i] = filePageCount[i] = -1;
		fileSlotCount *= 2;
		freeSlots = (int32_t*)malloc(fileSlotCount * sizeof(int32_t));
		freeSlotCount = 0;
		for (int i = 0; i < fileSlotCount; i++)
			if (firstPageOfFile[i] < 0)
				freeSlots[freeSlotCount++] = i;
		assert(freeSlotCount == fileSlotCount - fileCount);
	}

	// obtain file ID and setup file information
	int fileID = freeSlots[--freeSlotCount];
	PageInterval *freeInterval = (PageInterval*)freeNode->value;
	firstPageOfFile[fileID] = freeInterval->start;
	filePageCount[fileID] = fileSize / pageSize;

	// adjust free space information
	markAsOccupied(firstPageOfFile[fileID], filePageCount[fileID]);
	return fileID;
} // end of createFile(int)


int NonFragFileSystem::deleteFile(int fileID) {
	assert((fileID >= 0) && (fileID < fileSlotCount));
	assert(firstPageOfFile[fileID] >= 0);
	
	markAsFree(firstPageOfFile[fileID], filePageCount[fileID]);
	firstPageOfFile[fileID] = -1;
	filePageCount[fileID] = -1;
	freeSlots[freeSlotCount++] = fileID;

	return FILESYSTEM_SUCCESS;
} // end of deleteFile(int)


int NonFragFileSystem::readFile(int fileID, char *buffer, int off, int length) {
	assert((fileID >= 0) && (fileID < fileSlotCount));
	assert(firstPageOfFile[fileID] >= 0);
	
	off_t o = firstPageOfFile[fileID];
	o *= pageSize;
	o += off;
	lseek(fileHandle, o, SEEK_SET);
	int result = read(fileHandle, buffer, length);
	if (result >= 0)
		return result;
	else
		return FILESYSTEM_ERROR;
} // end of readFile(int, char*, int, int)


int NonFragFileSystem::writeFile(int fileID, char *buffer, int off, int length) {
	assert((fileID >= 0) && (fileID < fileSlotCount));
	assert(firstPageOfFile[fileID] >= 0);
	
	off_t o = firstPageOfFile[fileID];
	o *= pageSize;
	o += off;
	lseek(fileHandle, o, SEEK_SET);
	int result = write(fileHandle, buffer, length);
	if (result >= 0)
		return result;
	else
		return FILESYSTEM_ERROR;
} // end of writeFile(int, char*, int, int)


int NonFragFileSystem::getFileSize(int fileID) {
	assert((fileID >= 0) && (fileID < fileSlotCount));
	assert(firstPageOfFile[fileID] >= 0);
	return filePageCount[fileID] * pageSize;
} // end of getFileSize(int)


int NonFragFileSystem::copyFile(int file1, int off1, int file2, int off2, int length) {
	char *copyBuffer = (char*)malloc(COPYBUFFER_SIZE);
	while (length > 0) {
		int toRead = length;
		if (toRead > COPYBUFFER_SIZE)
			toRead = COPYBUFFER_SIZE;
		int result1 = readFile(file1, copyBuffer, off1, toRead);
		if (result1 != toRead) {
			free(copyBuffer);
			return FILESYSTEM_ERROR;
		}
		int result2 = writeFile(file2, copyBuffer, off2, toRead);
		if (result2 != toRead) {
			free(copyBuffer);
			return FILESYSTEM_ERROR;
		}
		off1 += toRead;
		off2 += toRead;
		length -= toRead;
	}
	free(copyBuffer);
	return FILESYSTEM_SUCCESS;
} // end of copyFile(int, int, int, int, int)


int NonFragFileSystem::getFirstPage(int fileID) {
	assert((fileID >= 0) && (fileID < fileSlotCount));
	assert(firstPageOfFile[fileID] >= 0);
	return firstPageOfFile[fileID];
} // end of getFirstPage(int)


void NonFragFileSystem::markAsOccupied(int start, int length) {
	// find free interval that contains the interval given by "start" and "end"
	PageInterval interval;
	interval.start = start;
	interval.length = length;
	GeneralAVLTreeNode *freeNode = freeSpaceSortedByPosition->findBiggestSmallerEq(&interval);
	assert(freeNode != NULL);

	// remove the interval descibed by "start" and "length" from the interval just
	// just found; reinsert the new interval into the search trees
	PageInterval *freeInterval = (PageInterval*)freeNode->value;
	assert(freeInterval->length >= length);
	PageInterval *newInterval = NULL;
	if (start == freeInterval->start) {
		if (length < freeInterval->length) {
			newInterval = (PageInterval*)malloc(sizeof(PageInterval));
			newInterval->start = freeInterval->start + length;
			newInterval->length = freeInterval->length - length;
			freeSpaceSortedByPosition->insertNode(newInterval);
			freeSpaceSortedBySize->insertNode(newInterval);
		}
	}
	else if (start + length == freeInterval->start + freeInterval->length) {
		if (length < freeInterval->length) {
			newInterval = (PageInterval*)malloc(sizeof(PageInterval));
			newInterval->start = freeInterval->start;
			newInterval->length = freeInterval->length - length;
			freeSpaceSortedByPosition->insertNode(newInterval);
			freeSpaceSortedBySize->insertNode(newInterval);
		}
	}
	else {
		newInterval = (PageInterval*)malloc(sizeof(PageInterval));
		newInterval->start = freeInterval->start;
		newInterval->length = start - freeInterval->start;
		freeSpaceSortedByPosition->insertNode(newInterval);
		freeSpaceSortedBySize->insertNode(newInterval);
		newInterval = (PageInterval*)malloc(sizeof(PageInterval));
		newInterval->start = start + length;
		newInterval->length =
			(freeInterval->start + freeInterval->length) - (start + length);
		freeSpaceSortedByPosition->insertNode(newInterval);
		freeSpaceSortedBySize->insertNode(newInterval);
	}

	// free the memory occupied by the old interval
	freeSpaceSortedByPosition->deleteNode(freeInterval);
	freeSpaceSortedBySize->deleteNode(freeInterval);
	free(freeInterval);
} // end of markAsOccupied(int, int)


void NonFragFileSystem::markAsFree(int start, int length) {

	// create new PageInterval object
	PageInterval *interval = (PageInterval*)malloc(sizeof(PageInterval));
	interval->start = start;
	interval->length = length;

	// compute left and right neighbor in search tree
	GeneralAVLTreeNode *leftNode =
		freeSpaceSortedByPosition->findBiggestSmallerEq(interval);
	GeneralAVLTreeNode *rightNode =
		freeSpaceSortedByPosition->findSmallestBiggerEq(interval);

	// check if intervals can be merged
	if (leftNode != NULL) {
		PageInterval *leftInterval = (PageInterval*)leftNode;
		if (leftInterval->start + leftInterval->length >= start) {
			// we can merge the new interval with the old interval
			assert(leftInterval->start + leftInterval->length == start);
			freeSpaceSortedByPosition->deleteNode(leftInterval);
			freeSpaceSortedByPosition->deleteNode(leftInterval);
			interval->start = leftInterval->start;
			interval->length += leftInterval->length;
			free(leftInterval);
		}
	}
	if (rightNode != NULL) {
		PageInterval *rightInterval = (PageInterval*)rightNode;
		if (start + length >= rightInterval->start) {
			// we can merge the new interval with the old interval
			assert(start + length == rightInterval->start);
			freeSpaceSortedByPosition->deleteNode(rightInterval);
			freeSpaceSortedByPosition->deleteNode(rightInterval);
			interval->length += rightInterval->length;
			free(rightInterval);
		}
	}

	// insert new interval into search trees
	freeSpaceSortedByPosition->insertNode(interval);
	freeSpaceSortedByPosition->insertNode(interval);
} // end of markAsFree(int, int)


void NonFragFileSystem::writeInt(int value) {
	forced_write(fileHandle, &value, sizeof(int));
}


int NonFragFileSystem::readInt() {
	int result;
	forced_read(fileHandle, &result, sizeof(int));
	return result;
} // end of readInt()


void NonFragFileSystem::increasePageCount(int newPageCount) {
	assert(newPageCount > pageCount);
	off_t newSize = newPageCount;
	newSize *= pageSize;
	if (ftruncate(fileHandle, newSize) < 0)
		assert("NonFragFileSystem could not change size." == NULL);
	markAsFree(pageCount, newPageCount - pageCount);
	pageCount = newPageCount;
} // end of increasePageCount(int)



