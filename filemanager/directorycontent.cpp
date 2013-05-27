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
 * created: 2005-02-20
 * changed: 2006-01-12
 **/


#include <math.h>
#include <stdio.h>
#include <string.h>
#include "directorycontent.h"
#include "data_structures.h"
#include "filemanager.h"
#include "../misc/all.h"


void initializeDirectoryContent(DirectoryContent *dc) {
	dc->count = 0;
	dc->longAllocated = 0;
	dc->longList = NULL;
	dc->shortCount = 0;
	dc->shortSlotsAllocated = 4;
	dc->shortList = typed_malloc(DC_ChildSlot, dc->shortSlotsAllocated);
} // end of initializeDirectoryContent(DirectoryContent*)


void initializeDirectoryContentFromChildList(DirectoryContent *dc,
			DC_ChildSlot *children, int count) {
	if (count == 0)
		initializeDirectoryContent(dc);
	else {
		dc->count = count;
		dc->longAllocated = count;
		dc->longList = typed_malloc(DC_ChildSlot, count);
		memcpy(dc->longList, children, count * sizeof(DC_ChildSlot));
		dc->shortCount = 0;
		dc->shortSlotsAllocated = 4;
		dc->shortList = typed_malloc(DC_ChildSlot, dc->shortSlotsAllocated);
	}
} // end of initializeDirectoryContentFromChildList(DirectoryContent*, int32_t*)


void freeDirectoryContent(DirectoryContent *dc) {
	if (dc->longList != NULL)
		free(dc->longList);
	dc->longList = NULL;
	if (dc->shortList != NULL)
		free(dc->shortList);
	dc->shortList = NULL;
	dc->count = -1;
} // end of freeDirectoryContent(DirectoryContent*)


static void sortList(DC_ChildSlot *list, int listLength) {
	if (listLength <= 1)
		return;
	else if (listLength == 2) {
		int hash0 = list[0].hashValue;
		int hash1 = list[1].hashValue;
		if ((hash1 < hash0) || ((hash1 == hash0) && (list[1].id < list[0].id))) {
			DC_ChildSlot temp = list[0];
			list[0] = list[1];
			list[1] = temp;
		}
	}
	else {
		int middle = listLength / 2;
		sortList(&list[0], middle);
		sortList(&list[middle], listLength - middle);
		DC_ChildSlot *result = typed_malloc(DC_ChildSlot, listLength);
		int pos1 = 0;
		int pos2 = middle;
		int outPos = 0;
		int hash1 = list[pos1].hashValue;
		int hash2 = list[pos2].hashValue;
		while (true) {
			if ((hash1 < hash2) || ((hash1 == hash2) && (list[pos1].id < list[pos2].id))) {
				result[outPos++] = list[pos1++];
				if (pos1 >= middle)
					break;
				hash1 = list[pos1].hashValue;
			}
			else {
				result[outPos++] = list[pos2++];
				if (pos2 >= listLength)
					break;
				hash2 = list[pos2].hashValue;
			}
		}
		while (pos1 < middle)
			result[outPos++] = list[pos1++];
		while (pos2 < listLength)
			result[outPos++] = list[pos2++];
		memcpy(list, result, listLength * sizeof(DC_ChildSlot));
		free(result);
	}
} // end of sortList(DC_ChildSlot*, int)


static DC_ChildSlot * mergeLists(DC_ChildSlot *longList, int longCount,
		DC_ChildSlot *shortList, int shortCount, int *resultLength) {

	// first, sort the short list to that we can merge it with the long guy
	if (shortCount > 0)
		sortList(shortList, shortCount);

	// merge the lists; remove everything that has been deleted
	// (DC_EMPTY_SLOT) from the resulting list
	DC_ChildSlot *result = typed_malloc(DC_ChildSlot, longCount + shortCount + 1);
	int longPos = 0;
	int shortPos = 0;
	int outPos = 0;
	while ((longPos < longCount) && (shortPos < shortCount)) {
		if ((longList[longPos].hashValue < shortList[shortPos].hashValue) ||
		    ((longList[longPos].hashValue == shortList[shortPos].hashValue) &&
		     (longList[longPos].id < shortList[shortPos].id)))
			result[outPos++] = longList[longPos++];
		else
			result[outPos++] = shortList[shortPos++];
		if (result[outPos - 1].id == DC_EMPTY_SLOT)
			outPos--;
	}
	while (longPos < longCount) {
		result[outPos] = longList[longPos++];
		if (result[outPos].id != DC_EMPTY_SLOT)
			outPos++;
	}
	while (shortPos < shortCount) {
		result[outPos] = shortList[shortPos++];
		if (result[outPos].id != DC_EMPTY_SLOT)
			outPos++;
	}

	// if we are wasting too much memory, realloc the whole thing
	if (outPos * 4 < (longCount + shortCount) * 3)
		typed_realloc(DC_ChildSlot, result, outPos + 1);

	*resultLength = outPos;
	return result;
} // end of mergeLists(...)


void mergeLists(DirectoryContent *dc) {
	if ((dc->shortCount == 0) && (dc->count == dc->longAllocated))
		return;

	// merge "longList" and "shortList" into one single list
	int newListLen;
	DC_ChildSlot *newList = mergeLists(dc->longList, dc->longAllocated,
			dc->shortList, dc->shortCount, &newListLen);
	if (dc->longList != NULL)
		free(dc->longList);
	dc->longList = newList;
	dc->longAllocated = newListLen;

	// allocate new memory for "additions" and "removals" arrays
	dc->shortSlotsAllocated = (int)sqrt(newListLen);
	if (dc->shortSlotsAllocated < 8)
		dc->shortSlotsAllocated = 8;
	if (dc->shortList != NULL)
		free(dc->shortList);
	dc->shortList = typed_malloc(DC_ChildSlot, dc->shortSlotsAllocated);
	dc->shortCount = 0;
} // end of mergeLists(DirectoryContent *dc)


static int binarySearch(int hashValue, DC_ChildSlot *list, int count) {
	if (count == 0)
		return -1;
	if ((list[0].hashValue > hashValue) || (list[count - 1].hashValue < hashValue))
		return -1;
	int lower = 0;
	int upper = count - 1;
	while (upper > lower) {
		int middle = (lower + upper) / 2;
		if (hashValue == list[middle].hashValue) {
			lower = upper = middle;
			break;
		}
		else if (hashValue > list[middle].hashValue)
			lower = middle + 1;
		else
			upper = middle - 1;
	}
	if (list[lower].hashValue == hashValue) {
		while (lower > 0) {
			if (list[lower - 1].hashValue == hashValue)
				lower--;
			else
				break;
		}
		return lower;
	}
	else
		return -1;
} // end of binarySearch(int, DC_ChildSlot*, int)


void addDirectoryToDC(int32_t id, DirectoryContent *dc, IndexedDirectory *directories) {
	if (dc->shortCount >= dc->shortSlotsAllocated)
		mergeLists(dc);
	// simply add the new id to the short list
	dc->shortList[dc->shortCount].id = -id;
	dc->shortList[dc->shortCount].hashValue = directories[id].hashValue;
	dc->shortCount++;
	dc->count++;
} // end of addDirectoryToDC(int32_t, DirectoryContent*, IndexedDirectory*)


void removeDirectoryFromDC(int32_t id, DirectoryContent *dc, IndexedDirectory *directories) {
	// first, check if the id is inside the short list; if so, simply remove
	// it; if not, find it in the long list and mark as deleted (DC_EMPTY_SLOT)
	for (int i = 0; i < dc->shortCount; i++)
		if (dc->shortList[i].id == -id) {
			dc->shortList[i] = dc->shortList[--dc->shortCount];
			dc->count--;
			return;
		}
	int index = binarySearch(directories[id].hashValue, dc->longList, dc->longAllocated);
	assert(index >= 0);
	while (index < dc->longAllocated) {
		if (dc->longList[index].id == -id) {
			dc->longList[index].id = DC_EMPTY_SLOT;
			dc->count--;
			return;
		}
		index++;
	}
	assert("We should never get here!" == NULL);
} // end of removeDirectoryFromDC(int32_t, DirectoryContent*, IndexedDirectory*)


void addFileToDC(int32_t id, DirectoryContent *dc, IndexedFile *files) {
	if (dc->shortCount >= dc->shortSlotsAllocated)
		mergeLists(dc);
	// simply add the new id to the short list
	dc->shortList[dc->shortCount].id = id;
	dc->shortList[dc->shortCount].hashValue = files[id].hashValue;
	dc->shortCount++;
	dc->count++;
} // end of addFileToDC(int32_t, DirectoryContent*, IndexedFile*)


void removeFileFromDC(int32_t id, DirectoryContent *dc, IndexedFile *files) {
	// first, check if the id is inside the short list; if so, simply remove
	// it; if not, find it in the long list and mark as deleted (DC_EMPTY_SLOT)
	for (int i = 0; i < dc->shortCount; i++)
		if (dc->shortList[i].id == id) {
			dc->shortList[i] = dc->shortList[--dc->shortCount];
			dc->count--;
			return;
		}
	int index = binarySearch(files[id].hashValue, dc->longList, dc->longAllocated);
	assert(index >= 0);
	while (index < dc->longAllocated) {
		if (dc->longList[index].id == id) {
			dc->longList[index].id = DC_EMPTY_SLOT;
			dc->count--;
			return;
		}
		index++;
	}
	assert("We should never get here!" == NULL);
} // end of removeFileFromDC(int32_t, DirectoryContent*, IndexedFile*)


int32_t findFileInDC(const char *name, DirectoryContent *dc, FileManager *fm) {
	int result = -1;
	int hashValue = FileManager::getHashValue((char*)name);

	// first, process the sorted list and search for the given name
	int count = 0;
	int index = binarySearch(hashValue, dc->longList, dc->longAllocated);
	if (index >= 0) {
		while (index < dc->longAllocated) {
			if (dc->longList[index].hashValue != hashValue)
				break;
			if ((dc->longList[index].id < 0) || (dc->longList[index].id == DC_EMPTY_SLOT)) {
				index++;
				continue;
			}
			IndexedFileOnDisk ifod;
			fm->readIFOD(dc->longList[index].id, &ifod);
			if (strcmp(name, ifod.fileName) == 0) {
				result = dc->longList[index].id;
				count++;
			}
			index++;
		}
	} // end if (index >= 0)

	// then, run over the unsorted list
	for (int i = 0; i < dc->shortCount; i++) {
		if (result >= 0) {
			if (dc->shortList[i].id == result)
				count++;
		}
		else if ((dc->shortList[i].hashValue == hashValue) && (dc->shortList[i].id >= 0)) {
			int id = dc->shortList[i].id;
			IndexedFileOnDisk ifod;
			fm->readIFOD(id, &ifod);
			if (strcmp(name, ifod.fileName) == 0) {
				result = id;
				count++;
			}
		}
	} // end for (int i = 0; i < dc->shortCount; i++)

	assert(count <= 1);
	return result;
} // end of findFileInDC(const char*, DirectoryContent*, FileManager*)


int32_t findDirectoryInDC(const char *name, DirectoryContent *dc, IndexedDirectory *directories) {
	int result = -1;
	int hashValue = FileManager::getHashValue((char*)name);

	// first, process the sorted list and search for the given name
	int count = 0;
	int index = binarySearch(hashValue, dc->longList, dc->longAllocated);
	if (index >= 0) {
		while (index < dc->longAllocated) {
			if (dc->longList[index].hashValue != hashValue)
				break;
			if ((dc->longList[index].id >= 0) || (dc->longList[index].id == DC_EMPTY_SLOT)) {
				index++;
				continue;
			}
			if (strcmp(name, directories[-(dc->longList[index].id)].name) == 0) {
				result = -dc->longList[index].id;
				count++;
			}
			index++;
		}
	} // end if (index >= 0)

	// then, run over the unsorted list
	for (int i = 0; i < dc->shortCount; i++) {
		if (result >= 0) {
			if (dc->shortList[i].id == -result)
				count++;
		}
		else if ((dc->shortList[i].hashValue == hashValue) && (dc->shortList[i].id < 0)) {
			int id = -dc->shortList[i].id;
			if (strcmp(name, directories[id].name) == 0) {
				result = id;
				count++;
			}
		}
	} // end for (int i = 0; i < dc->shortCount; i++)

	assert(count <= 1);
	return result;
} // end of findDirectoryInDC(const char*, DirectoryContent*, IndexedDirectory*)



