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
 * The use of this alloc implementation helps finding memory lacks etc.
 *
 * author: Stefan Buettcher
 * created: 2004-05-21
 * changed: 2007-02-12
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lockable.h"
#include "logging.h"


/** 
 * For every malloc call, an Allocation instance we will be stored in the
 * hashtable. Collisions are resolved by linked lists.
 **/
typedef struct {
	const char *file;  // in what file can we find the call to malloc?
	int line;          // in what line, to be specific?
	long ptr, size;    // the memory allocated: address and length
	int timeStamp;     // index number, used for conditional dumps
	void *next;        // for collisions in the hashtable: the succ's position
} Allocation;


static const char *LOG_ID = "Allocator";

/** This is an arbitrary prime number. Feel free to change it. **/
static const int HASHTABLE_SIZE = 1234577;

/**
 * Before and after every allocated memory block, we have a safety zone which
 * we fill with '\0' characters. If their value has changed when free(...) is
 * called, we report an error.
 **/
static const int SAFETY_ZONE_SIZE = 16;

/** We will store memory allocation information in this hashtable. **/
static Allocation *hashtable[HASHTABLE_SIZE];

/**
 * In case we have a deallocation (free) conflict, we want to know who has
 * freed the memory we are trying to free. The following hashtable can help
 * us there.
 **/
typedef struct {
	const char *file;
	int line;
} Deallocation;

static const int SMALL_HASHTABLE_SIZE = 12347;

static Deallocation deallocHashtable[SMALL_HASHTABLE_SIZE];


/** Well, you know... **/
static bool initialized = false;

/** Counter, to be increased. For conditional dumps. **/
static int timeStamp = 0;

/** Number of currently active allocations. **/
static int allocationCnt = 0;

/** Number of bytes allocated. **/
static long bytesAllocated = 0;

/** Maximum number of bytes allocated at the same time so far. **/
static long maxAllocated = 0;

static Lockable *lock;


/**
 * If debugging has been actived in alloc.h, this function is called everytime
 * somebody wants to do a malloc. Its pushes a new Allocation instance into the
 * hashtable.
 **/
void *debugMalloc(int size, const char *file, int line) {
	char message[256];
	if (size <= 0) {
		sprintf(message, "Trying to allocate 0 bytes at %s/%i.", file, line);
		log(LOG_ERROR, LOG_ID, message);
	}
	assert(size > 0);
	if (!initialized) {
		memset(hashtable, 0, sizeof(hashtable));
		memset(deallocHashtable, 0, sizeof(deallocHashtable));
		initialized = true;
		lock = new Lockable();
	}

	lock->getLock();

	char *realPtr = new char[size + 2 * SAFETY_ZONE_SIZE];
	if (realPtr == NULL) {
		sprintf(message, "Trying to allocate %i bytes at %s/%i.", size, file, line);
		log(LOG_ERROR, LOG_ID, message);
		sprintf(message, "Number of active allocations: %i.", allocationCnt);
		log(LOG_ERROR, LOG_ID, message);
		assert(realPtr != NULL);
	}
	memset(realPtr, 0, size + 2 * SAFETY_ZONE_SIZE);
	void *result = &realPtr[SAFETY_ZONE_SIZE];
	
	int hashValue = ((long)result) % HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	Allocation *a = new Allocation;
	assert(a != NULL);
	a->timeStamp = timeStamp;
	a->file = file;
	a->line = line;
	a->ptr = (long)result;
	a->size = size;
	a->next = hashtable[hashValue];
	hashtable[hashValue] = a;
	allocationCnt++;
	timeStamp++;
	
	bytesAllocated += size;
	if (bytesAllocated > maxAllocated)
		maxAllocated = bytesAllocated;

	lock->releaseLock();
	return result;
} // end of debugMalloc(int, char*, int)


/**
 * If debugging has been actived in alloc.h, this function is called everytime
 * somebody wants to do a free. It looks for the Allocation instance that
 * describes the memory that is about to be freed. If no such descriptor can
 * be found, it writes an error message to stderr and terminates the program
 * execution with exit code -1. This function is NULL-pointer save.
 **/
void debugFree(void *ptr, const char *file, int line) {
	lock->getLock();

	assert(ptr != NULL);
	long address = (long)ptr;
	int hashValue = address % HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	Allocation *runner = hashtable[hashValue];
	Allocation *prev = NULL;
	while (runner != NULL) {
		if (runner->ptr == address) {
			if (prev == NULL)
				hashtable[hashValue] = (Allocation*)runner->next;
			else
				prev->next = (Allocation*)runner->next;

			char *charPtr = (char*)ptr;
			charPtr = &charPtr[-SAFETY_ZONE_SIZE];
			bool ok = true;
			for (int i = 0; i < SAFETY_ZONE_SIZE; i++)
				if ((charPtr[i] != 0) || (charPtr[runner->size + i + SAFETY_ZONE_SIZE] != 0))
					ok = false;
			if (!ok) {
				char message[256];
				sprintf(message, "Memory allocated at %ld: Write beyond array boundaries.", (long)ptr);
				log(LOG_ERROR, LOG_ID, message);
				sprintf(message, "Allocated by %s/%i.", runner->file, runner->line);
				log(LOG_ERROR, LOG_ID, message);
				sprintf(message, "Being freed by %s/%i.", file, line);
				log(LOG_ERROR, LOG_ID, message);
				assert(false);
			}
			long thisSize = runner->size;
			delete runner;
			delete[] charPtr;
			allocationCnt--;

			// update deallocation hashtable
			hashValue = address % SMALL_HASHTABLE_SIZE;
			if (hashValue < 0)
				hashValue = -hashValue;
			deallocHashtable[hashValue].file = file;
			deallocHashtable[hashValue].line = line;

			bytesAllocated -= thisSize;
			lock->releaseLock();
			return;
		}
		prev = runner;
		runner = (Allocation*)runner->next;
	}

	// if we get here, that means we have an allocation error
	char message[256];
	sprintf(message, "%s/%i is trying to free data at %ld, which is not in the allocation table.",
			file, line, address);
	log(LOG_ERROR, LOG_ID, message);
	hashValue = address % SMALL_HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	if (deallocHashtable[hashValue].file != NULL) {
		sprintf(message, "It has probably been freed by %s/%i.",
				deallocHashtable[hashValue].file, deallocHashtable[hashValue].line);
		log(LOG_ERROR, LOG_ID, message);
	}
	lock->releaseLock();
	assert(false);
} // end of debugFree(void*)


void *debugRealloc(void *ptr, int size, const char *file, int line) {
	if (ptr == NULL)
		return debugMalloc(size, file, line);
	lock->getLock();

	long address = (long)ptr;
	int hashValue = address % HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	Allocation *runner = hashtable[hashValue];
	int oldSize = -1;
	while (runner != NULL) {
		if (runner->ptr == address) {
			oldSize = runner->size;
			break;
		}
		runner = (Allocation*)runner->next;
	}
	if (oldSize < 0) {
		char message[256];
		sprintf(message, "Problem reallocating data (already freed?): %s/%i", file, line);
		log(LOG_ERROR, LOG_ID, message);
		assert(false);
	}

	lock->releaseLock();

	void *result = debugMalloc(size, file, line);
	memcpy(result, ptr, (oldSize < size ? oldSize : size));
	debugFree(ptr, file, line);
	return result;
} // end of debugRealloc(void*, int, char*, int)


/** Returns the current time stamp. **/
int getAllocTimeStamp() {
	return timeStamp;
} // end of getAllocTimeStamp()


static int printAllocation(Allocation *a, int param1, int param2, int value) {
	if ((a->timeStamp > param1) && (a->timeStamp < param2))
		fprintf(stderr, "(Allocator) %s/%d: %ld bytes at address %lu (timestamp: %u)\n",
			a->file, a->line, a->size, a->ptr, a->timeStamp);
	return 0;
} // end of printAllocation(Allocation*, int, int, int)


static int countAllocations(Allocation *a, int p1, int p2, int value) {
	return value + 1;
} // end of countAllocations(Allocation*, int, int, int)


static int countAllocationSize(Allocation *a, int p1, int p2, int value) {
	return value + a->size;
} // end of countAllocationSize(Allocation*, int, int, int)


static int forAllAllocations(int (*visitor)(Allocation *a,
		int param1, int param2, int value), int p1, int p2) {
	int result = 0;
	for (int i = 0; i < HASHTABLE_SIZE; i++) {
		Allocation *runner = hashtable[i];
		while (runner != NULL) {
			result = visitor(runner, p1, p2, result);
			runner = (Allocation*)runner->next;
		}
	}
	return result;
} // end of forAllAllocations(...)


/** Returns the cumulated size of all active allocations. **/
int getAllocationSize() {
	return forAllAllocations(countAllocationSize, 0, 0);
} // end of getAllocationSize()


/** Returns the number of active allocations. **/
int getAllocationCount() {
	return allocationCnt;
} // end of getAllocationCount()


/**
 * Prints all Allocation descriptors that have been created after "timeStamp"
 * and whose memory has not been freed yet.
 **/
void printAllocationsAfter(int timeStamp) {
	forAllAllocations(printAllocation, timeStamp, 2000000000);
} // end of printAllocationsAfter(int)


/** Prints all Allocation descriptors whose memory has not been freed. **/
void printAllocations() {
	forAllAllocations(printAllocation, -1, 2000000000);
} // end of printAllocations()


/**
 * Prints all Allocation descriptors that have been created before "timeStamp"
 * and whose memory has not been freed yet.
 **/
void printAllocationsBefore(int timeStamp) {
	forAllAllocations(printAllocation, -1, timeStamp);
} // end of printAllocationsBefore(int)


long getMaxAllocated() {
	return maxAllocated;
}


void setMaxAllocated(long newMax) {
	maxAllocated = newMax;
}


#undef free

void realFree(void *ptr) {
	free(ptr);
}


