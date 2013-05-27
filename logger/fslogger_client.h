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
 * Header file for fslogger clients. Registration with the daemon and
 * message reception is done using the functions defined here.
 *
 * author: Stefan Buettcher
 * created: 2005-03-05
 * changed: 2005-03-05
 **/


#ifndef __FSLOGGER__CLIENT__H
#define __FSLOGGER__CLIENT__H


#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>


/** This is the key that people use to obtain a handle to our message queue. **/
static const int MSG_QUEUE_KEY = 0x0e1e4a;
                                                                                                      
/** Various message types. **/
static const long MESSAGE_SHUTDOWN = 0x01;
static const long MESSAGE_ACKNOWLEDGE = 0x02;
static const long MESSAGE_REGISTER = 0x03;
static const long MESSAGE_UNREGISTER = 0x04;
static const long MESSAGE_PLEASE_REFRESH = 0x05;
static const long MESSAGE_FSCHANGE = 0x06;
static const long MESSAGE_STATUS = 0x07;

/** Length of an empty message (without body string). **/
static const int EMPTY_MESSAGE_LENGTH = sizeof(long) + sizeof(uid_t) + 2 * sizeof(int);

/** Maximum length of the message body. **/
static const int MAX_MESSAGE_STRING = 256;

typedef struct {

	/** Type of the message, as defined above. **/
	long messageType;

	/** UID of the owner of the calling process. **/
	uid_t userID;

	/**
	 * Every message to the daemon has to contain the ID of a queue that can be
	 * used to deliver the response. A response is only sent if:
	 *
	 *  - the sender of the message ("userID") equals the owner of the queue;
	 *  - the queue owner is the only user that may read from the queue.
	 **/
	int queueID;

	/** Length of the string that follows (including the trailing 0). **/
	int bodyLength;

	/** The message itself. **/
	char messageString[MAX_MESSAGE_STRING];

} FSChangeMessage;


class FSLoggerClient {

private:

	/** Local queue, used to receive notifications. **/
	int localMessageQueue;

	/** Daemon's message queue, used to send messages to the daemon. **/
	int remoteMessageQueue;

	/** Do a bit of concurrency awareness. **/
	sem_t mutex;

	/** Tells us whether we are currently registered for file system change notification. **/
	bool registered;

public:

	FSLoggerClient();

	~FSLoggerClient();

	/**
	 * Registers with the fslogger daemon for file system changes. Returns true
	 * iff registration was successful.
	 **/
	bool registerWithDaemon();

	/** Unregisters with the daemon. **/
	bool unregister();

	/** Returns true iff we have registered for file system changes. **/
	bool isRegistered();

	/**
	 * Returns an fschange notification string or NULL if the daemon is not active.
	 * If the return value is non-NULL, memory has to be freed by the caller.
	 **/
	char *receiveNotification();

}; // end of class FSLoggerClient


#endif
