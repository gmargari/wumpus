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
 * Implementation of the Lockable class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-07
 * changed: 2009-02-01
 **/


#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "lockable.h"
#include "../misc/all.h"


/**
 * This variable is used to determine whether the locking mechanism is
 * actually enabled or not. The reason why we need this is because when we spawn
 * a new process, it is possible that some of the locks are held by a different
 * thread. So, we simply disable locking for the new process.
 **/
static bool lockingEnabled = true;

static const char * LOG_ID = "Lockable";


Lockable::Lockable() {
	maxSimultaneousReaders = MAX_SIMULTANEOUS_READERS;
	SEM_INIT(internalDataSemaphore, 1);
	SEM_INIT(readWriteSemaphore, maxSimultaneousReaders);
	sem_wait(&internalDataSemaphore);
	for (int i = 0; i < maxSimultaneousReaders; i++)
		readLockFree[i] = true;
	writeLockFree = true;
	lockHolder = (pthread_t)0;
	sem_post(&internalDataSemaphore);
} // end of Lockable()


Lockable::~Lockable() {
	sem_wait(&internalDataSemaphore);
	sem_destroy(&readWriteSemaphore);
	sem_destroy(&internalDataSemaphore);
} // end of ~Lockable()


void Lockable::disableLocking() {
	lockingEnabled = false;
} // end of disableLocking()


void Lockable::setMaxSimultaneousReaders(int value) {
	if (value < 1)
		value = 1;
	if (value > 64)
		value = 64;
	sem_wait(&internalDataSemaphore);
	for (int i = 0; i < maxSimultaneousReaders; i++)
		if ((!readLockFree[i]) || (!writeLockFree)) {
			assert("Impossible to change the maximum number of simultaneous readers." == NULL);
			sem_post(&internalDataSemaphore);
			return;
		}
	maxSimultaneousReaders = value;
	for (int i = 0; i < maxSimultaneousReaders; i++)
		readLockFree[i] = true;
	writeLockFree = true;
	sem_post(&internalDataSemaphore);
} // end of setMaxSimultaneousReaders(int)


bool Lockable::hasReadLock() {
	if (!lockingEnabled)
		return false;
	sem_wait(&internalDataSemaphore);
	pthread_t thisThread = pthread_self();
	for (int i = 0; i < maxSimultaneousReaders; i++)
		if ((readLockFree[i] == false) &&
		    (pthread_equal(readLockHolders[i], thisThread))) {
			sem_post(&internalDataSemaphore);
			return true;
		}
	sem_post(&internalDataSemaphore);
	return false;
} // end of hasReadLock()


bool Lockable::getReadLock() {
	if ((!lockingEnabled) || (hasReadLock()))
		return false;
	sem_wait(&readWriteSemaphore);
	sem_wait(&internalDataSemaphore);
	for (int i = 0; i < maxSimultaneousReaders; i++)
		if (readLockFree[i]) {
			readLockFree[i] = false;
			readLockHolders[i] = pthread_self();
			break;
		}
	sem_post(&internalDataSemaphore);
	return true;
} // end of getReadLock()


void Lockable::releaseReadLock() {
	if (!lockingEnabled)
		return;
	pthread_t thisThread = pthread_self();
	sem_wait(&internalDataSemaphore);
	for (int i = 0; i < maxSimultaneousReaders; i++)
		if (readLockFree[i] == false)
			if (pthread_equal(readLockHolders[i], thisThread)) {
				readLockFree[i] = true;
				sem_post(&readWriteSemaphore);
			}
	sem_post(&internalDataSemaphore);
} // end of releaseReadLock()


bool Lockable::hasWriteLock() {
	if (!lockingEnabled)
		return false;
	pthread_t thisThread = pthread_self();
	if ((!writeLockFree) && (pthread_equal(writeLockHolder, thisThread)))
		return true;
	else
		return false;
} // end of hasWriteLock()


bool Lockable::getWriteLock() {
	if ((!lockingEnabled) || (hasWriteLock()))
		return false;
	for (int i = 0; i < maxSimultaneousReaders; i++)
		sem_wait(&readWriteSemaphore);
	sem_wait(&internalDataSemaphore);
	writeLockFree = false;
	writeLockHolder = pthread_self();
	sem_post(&internalDataSemaphore);
	return true;
} // end of getWriteLock()


void Lockable::releaseWriteLock() {
	if ((!lockingEnabled) || (!hasWriteLock()))
		return;
	sem_wait(&internalDataSemaphore);
	writeLockFree = true;
	writeLockHolder = (pthread_t)0;
	sem_post(&internalDataSemaphore);
	for (int i = 0; i < maxSimultaneousReaders; i++)
		sem_post(&readWriteSemaphore);
} // end of releaseWriteLock()


void Lockable::releaseAnyLock() {
	if (!lockingEnabled)
		return;
	releaseReadLock();
	releaseWriteLock();
	releaseLock();
} // end of releaseAnyLock()


bool Lockable::getLock() {
	if (!lockingEnabled)
		return false;
//int a = pthread_self(), b = getpid();
//char className[64];
//getClassName(className);
//printf("%u/%u trying to acquire lock: %s\n", a, b, className);
	pthread_t thisThread = pthread_self();
	if (lockHolder == thisThread) {
//printf("  %u already has the lock: %s\n", a, className);
		return false;
	}
	sem_wait(&internalDataSemaphore);
	lockHolder = thisThread;
//printf("  %u acquired the lock: %s\n", a, className);
	return true;
} // end of getLock()


void Lockable::releaseLock() {
	if (!lockingEnabled)
		return;
	if (lockHolder != pthread_self())
		return;
//	char className[64];
//	getClassName(className);
//	printf("%u/%u releases the lock: %s\n", (int)pthread_self(), (int)getpid(), className);
	lockHolder = (pthread_t)0;
	sem_post(&internalDataSemaphore);
} // end of releaseLock()


void Lockable::getClassName(char *target) {
	strcpy(target, "Lockable");
}


LocalLock::LocalLock(Lockable *lockable) {
	this->lockable = lockable;
	mustReleaseLock = lockable->getLock();
}


LocalLock::~LocalLock() {
	if (mustReleaseLock)
		lockable->releaseLock();
}


