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
 * Header file to alloc.cpp, the "forgotten malloc" finder.
 *
 * author: Stefan Buettcher
 * created: 2004-05-21
 * changed: 2007-02-12
 **/


#include <stdlib.h>
#include "../config/config.h"


#if ALLOC_DEBUG
	// redefine malloc and free for debugging purposes
	#define malloc(size) debugMalloc(size, __FILE__, __LINE__)
	#define free(ptr) debugFree(ptr, __FILE__, __LINE__)
	#define realloc(ptr, size) debugRealloc(ptr, size, __FILE__, __LINE__)
	#define typed_malloc(type, num) \
		(type*)debugMalloc((num) * sizeof(type), __FILE__, __LINE__)
	#define typed_realloc(type, ptr, num) \
		ptr = (type*)debugRealloc(ptr, (num) * sizeof(type), __FILE__, __LINE__)
#else
	// define the typed allocation routines
	#define typed_malloc(type, num) (type*)malloc((num) * sizeof(type))
	#define typed_realloc(type, ptr, num) ptr = (type*)realloc(ptr, (num) * sizeof(type))
#endif


#ifndef __MISC__ALLOC_H_
#define __MISC__ALLOC_H_


#define FREE_AND_SET_TO_NULL(p) if (p != NULL) { free(p); p = NULL; }


/**
 * If debugging has been actived in alloc.h, this function is called everytime
 * somebody wants to do a malloc. Its pushes a new Allocation instance into the
 * hashtable.
 **/	
void *debugMalloc(int size, const char *file, int line);

/**
 * If debugging has been actived in alloc.h, this function is called everytime
 * somebody wants to do a free. It looks for the Allocation instance that
 * describes the memory that is about to be freed. If no such descriptor can
 * be found, it writes an error message to stderr and terminates the program
 * execution with exit code -1. This function is NULL-pointer save.
 **/
void debugFree(void *ptr, const char *file, int line);

/**
 * This function calls the "real" free function (from the malloc library)
 * instead of my macro. Sometimes this is necessary, for instance when we have
 * to free buffers allocated by readline.
 **/
void realFree(void *ptr);

void *debugRealloc(void *ptr, int size, const char *file, int line);

/** Returns the current time stamp. **/
int getAllocTimeStamp();

/**
 * Prints all Allocation descriptors that have been created after "timeStamp"
 * and whose memory has not been freed yet.
 **/
void printAllocationsAfter(int timeStamp);

/** Prints all Allocation descriptors whose memory has not been freed. **/
void printAllocations();

/**
 * Prints all Allocation descriptors that have been created before "timeStamp"
 * and whose memory has not been freed yet.
 **/
void printAllocationsBefore(int timeStamp);

int getAllocationCount();

int getAllocationSize();

long getMaxAllocated();

void setMaxAllocated(long newMax);

#endif


