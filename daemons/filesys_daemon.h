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
 * The FileSysDaemon class implements a daemon that permanently checks if
 * the file system can tell new stories. If this is the case, everybody starts
 * dancing, and the indexing system can apply all appropriate updates. There
 * are 4 different ways how the FileSysDaemon gets to know about changes:
 *
 *  - if the fschange kernel patch has been applied and the fschange module
 *    has been loaded, all necessary information can be obtained by reading
 *    the proc file /proc/fschange
 *
 *  - if fschange is installed and the fschange_logger daemon is running,
 *    /proc/fschange must not be read directly, but the data can be obtained
 *    by registering with the fschange_logger instead
 *
 *  - if fschange is not installed, but the kernel supports inotify, we use
 *    inotify to keep track of file system status
 *
 *  - if neither fschange not inotify are installed, we have to do periodical
 *    file system scans to update the index.
 *
 * author: Stefan Buettcher
 * created: 2004-10-08
 * changed: 2009-02-01
 **/


#ifndef __DAEMONS__FILESYS_DAEMON_H
#define __DAEMONS__FILESYS_DAEMON_H


#include "daemon.h"
#include "eventqueue.h"
#include "../index/index.h"
#include "../logger/fslogger_client.h"
#include <pthread.h>
#include <semaphore.h>


/**
 * INotifyHashtableElement is used to translate the INotify directory IDs to
 * directory path names.
 **/
typedef struct {

	int id;

	char *directoryName;

	void *prev, *next;

} INotifyHashtableElement;


class FileSysDaemon : public Daemon {

public:

	/**
	 * Usually, we scan the filesystem once a day (60 * 24 = 1440 minutes) for
	 * changed files.
	 **/
	static const int DEFAULT_SCAN_INTERVAL = 1440;

	/** Four possible run modes, as described above. **/
	static const int RUN_MODE_FSCHANGE_LOGGER = 1;
	static const int RUN_MODE_FSCHANGE_DIRECT = 2;
	static const int RUN_MODE_INOTIFY = 3;
	static const int RUN_MODE_DISK_SCAN = 4;

	static const int WAIT_INTERVAL = 2;

	static const int DISK_SCAN_WAIT_INTERVAL = 100;

	static const int EVENT_WAIT_INTERVAL = 200;

	/** "FileSysDaemon". **/
	static const char *LOG_ID;

	/** Size of the buffer used when reading stuff from the fschange proc file. **/
	static const int PROC_BUFFER_SIZE = 65536;

	/**
	 * INotify events come as 32-bit integers. We use a hashtable to translate the
	 * integer numbers to full path names.
	 **/
	static const int INOTIFY_HASHTABLE_SIZE = 12347;

protected:

	/**
	 * This is the time at which the last file system scan was completed. Used
	 * for periodic scans of the entire file system. Accessed from within
	 * MasterIndex.
	 **/
	time_t lastScan;

private:

	/** The Index instance that created us. **/
	Index *owner;

	/** This guy is used if the FSLogger daemon is running. **/
	FSLoggerClient *fsClient;

	/** File handle to the /proc/fschange file. **/
	int procFile;

	/** Used to read stuff from the proc file. **/
	char readBuffer[PROC_BUFFER_SIZE];

	/** Used to asynchronously process file system events. **/
	EventQueue *eventQueue;

	/**
	 * Base directory for this file system watcher. Only events below this
	 * directory will be processed.
	 **/
	char *baseDir;

	/** The file that we read file system events from. **/
	char fschangeFile[MAX_CONFIG_VALUE_LENGTH];

	/** Tells us whether we are currently scanning a subtree of the file system. **/
	bool isScanning;

public:

	/** Creates a new FileSysDaemon with default parameters. **/
	FileSysDaemon(Index *index, char *baseDirectory = NULL, time_t lastScan = 0);

	/** Stops the execution of the daemon and frees all resources. **/
	virtual ~FileSysDaemon();

	virtual void run();

	virtual void stop();

	virtual void scanFileSystem();

private:

	/**
	 * Spawns a new thread to execute an UpdateQuery that informs the Index
	 * of the file system change that has happened.
	 **/
	void notifyIndex(char *event, time_t timeStamp);

	/**
	 * This function is called right at the beginning. It reads /etc/mtab in order
	 * to check all mount points and create the appropriate indexes.
	 **/
	void checkMountPoints();

	int scanDirectory(char *baseDir, bool recursive);

	void processEvents();

}; // end of class FileSysDaemon


#endif



