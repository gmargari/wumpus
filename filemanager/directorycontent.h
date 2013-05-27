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
 * The DirectoryContent structure is used to maintain directory contents:
 * files and subdirectories. In order to keep memory requirements low, we
 * do not have a binary search tree but a long sorted and a short unsorted
 * lists of IDs . The short list is of length sqrt(n), where n is the length
 * of the long list. This way, we can search in time O(sqrt(n)), which is
 * acceptable for directories that contain less that ~10000 children.
 *
 * A DirectoryContent object contains a bunch of file and directory IDs.
 * Positive ID values refer to files, negative ID values refer to directories.
 *
 * author: Stefan Buettcher
 * created: 2005-02-20
 * changed: 2006-01-12
 **/


#ifndef __FILEMANAGER__DIRECTORYCONTENT_H
#define __FILEMANAGER__DIRECTORYCONTENT_H


#include <sys/types.h>
#include "data_structures.h"
#include "../misc/all.h"


class FileManager;


void initializeDirectoryContent(DirectoryContent *dc);

void initializeDirectoryContentFromChildList(DirectoryContent *dc,
			DC_ChildSlot *children, int count);

void freeDirectoryContent(DirectoryContent *dc);

/**
 * Adds the directory given by "id" to the DirectoryContent instance given
 * by "dc". Parent information in new child is automatically updated.
 **/
void addDirectoryToDC(int32_t id, DirectoryContent *dc, IndexedDirectory *directories);

void removeDirectoryFromDC(int32_t id, DirectoryContent *dc, IndexedDirectory *directories);

/**
 * Adds the file given by "id" to the DirectoryContent instance given
 * by "dc". Parent information in new child is automatically updated. Modified
 * information is automatically synced with on-disk data.
 **/
void addFileToDC(int32_t id, DirectoryContent *dc, IndexedFile *files);

void removeFileFromDC(int32_t id, DirectoryContent *dc, IndexedFile *files);

/**
 * Merges the two lists inside the DirectoryContent object "dc" (sorted, unsorted)
 * into one big sorted list.
 **/
void mergeLists(DirectoryContent *dc);

/**
 * Returns the file ID of the file specified by "name", if existent inside the
 * given directory, or -1 if non-existent.
 **/
int32_t findFileInDC(const char *name, DirectoryContent *dc, FileManager *fm);

/**
 * Returns the directory ID of the directory specified by "name", if existent
 * inside the given directory, of -1 if non-existent.
 **/
int32_t findDirectoryInDC(const char *name, DirectoryContent *dc, IndexedDirectory *directories);


#endif


