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
 * The Lockable class offers a way to cope with concurrent modification of
 * sensitive data. If a class needs protection of local data, you can simply
 * add Lockable to the list of superclasses and use the methods provided by
 * the Lockable class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-07
 * changed: 2006-09-19
 **/


#ifndef __MISC__LOCKABLE_H
#define __MISC__LOCKABLE_H


#include <assert.h>
#include <pthread.h>
#include <semaphore.h>


class Lockable {

public:

	/** Default value for maximum of simultaenous reader processes. **/
	static const int MAX_SIMULTANEOUS_READERS = 4;

	/**
	 * When shutdown() is called, we have to wait until all people that are
	 * currently using this resource have finished their execution.
	 * SHUTDOWN_WAIT_INTERVAL is the interval between two subsequent poll operations.
	 **/
	static const int SHUTDOWN_WAIT_INTERVAL = 10;

protected:

	/** Used for error logging. Not thread-safe. **/
	char errorMessage[256];

private:

	/** Maximum number of concurrent readers allowed. **/
	int maxSimultaneousReaders;

	/** Tells us for every read lock slot if it is still available. **/
	bool readLockFree[64];

	/** Tells us for every read lock slot who currently owns it. **/
	pthread_t readLockHolders[64];

	/** Tells us if the write lock is still free. **/
	bool writeLockFree;

	/** Tells us who the holder of the write lock is. **/
	pthread_t writeLockHolder;

	/** Holder of the mutex. **/
	pthread_t lockHolder;

	/**
	 * This semaphore is used in all places where the use of the read/write lock
	 * is inappropriate, for instance when we are messing with the locks themselves.
	 **/
	sem_t internalDataSemaphore;

	/**
	 * Used to simulate a read/write lock. I use a semaphore here instead of the
	 * lock because locks don't support max-user limits.
	 **/
	sem_t readWriteSemaphore;

public:

	Lockable();

	virtual ~Lockable();

	/** Returns true iff the calling process holds a read lock. **/
	bool hasReadLock();

	/**
	 * Acquires a read lock. If at least one process holds a read lock, no other
	 * process may change the index data. If the process already holds a read
	 * lock, nothing happens. If it holds a write lock, the write lock is released
	 * first. This method returns true if the process did not have a read lock
	 * before the call; false otherwise.
	 **/
	bool getReadLock();

	/**
	 * Releases a previously acquired read lock. If the process does not hold a
	 * read lock, nothing happens.
	 **/
	void releaseReadLock();

	/** Returns true iff the calling process holds the write lock. **/
	bool hasWriteLock();

	/**
	 * Acquires a write lock. When a process holds the write lock, no other process
	 * is allowed to read index data. If the process already holds the write lock,
	 * nothing happens. If it holds a read lock, the read lock is released first.
	 * This method returns true if the thread did not have the lock before the
	 * call; false otherwise.
	 **/
	bool getWriteLock();

	/**
	 * Releases the previously acquired write lock. If the process does not hold
	 * the write lock, nothing happens.
	 **/
	void releaseWriteLock();

	/**
	 * Releases the currently held lock, regardless of whether it is a read or a
	 * write lock. If the process does not hold a lock, nothing happens.
	 **/
	void releaseAnyLock();

	/** Sets the value of the "readLockSupported" member variable. **/
	void setReadLockSupported(bool value);

	/** Acquires the mutex. Returns false if the process already has the mutex. **/
	bool getLock();

	/** Releases the mutex. **/
	void releaseLock();

	/**
	 * Disables the locking mechanism for the current process. This applies to
	 * all threads that might belong to the process.
	 **/
	static void disableLocking();

	/** Puts the class name of the object into the buffer referenced by "target". **/
	virtual void getClassName(char *target);

protected:

	/** Sets the maximum number of concurrent readers for this object. **/
	void setMaxSimultaneousReaders(int value);

}; // end of class Lockable


class LocalLock {

private:

	/** The Lockable object that we hold a lock for. **/
	Lockable *lockable;

	/**
	 * Tells us whether we actually hold the lock or not and whether we have to
	 * release it in the destructor.
	 **/
	bool mustReleaseLock;

public:
	
	/** Acquires a local lock for the given object. **/
	LocalLock(Lockable *lockable);

	/** Releases the lock. **/
	~LocalLock();

}; // end of class LocalLock


#endif


