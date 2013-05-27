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
 * Implementation of some functions that are needed for inter-process
 * communication with the logging daemon.
 *
 * author: Stefan Buettcher
 * created: 2005-03-04
 * changed: 2005-03-06
 **/


#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include "fslogger.h"
#include "../misc/all.h"


FSLoggerClient::FSLoggerClient() {
	sem_init(&mutex, 0, 1);
	localMessageQueue = -1;
	remoteMessageQueue = -1;
	registered = false;
	remoteMessageQueue = msgget(MSG_QUEUE_KEY, S_IWUSR);
} // end of FSLoggerClient()


FSLoggerClient::~FSLoggerClient() {
	sem_wait(&mutex);
	if (registered) {
		sem_post(&mutex);
		unregister();
	}
	if (localMessageQueue >= 0) {
		msgctl(localMessageQueue, IPC_RMID, NULL);
		localMessageQueue = -1;
	}
} // end of ~FSLoggerClient()


bool FSLoggerClient::registerWithDaemon() {
	sem_wait(&mutex);
	if (remoteMessageQueue < 0) {
		sem_post(&mutex);
		return false;
	}
	if (localMessageQueue < 0) {
		// create local queue
		localMessageQueue = msgget(IPC_PRIVATE, S_IRUSR | S_IXUSR | S_IWUSR);
		if (localMessageQueue >= 0) {
			// adjust access permissions so that the daemon accepts the queue
			struct msqid_ds msgParams;
			msgctl(localMessageQueue, IPC_STAT, &msgParams);
			msgParams.msg_qbytes = 512;
			msgParams.msg_perm.mode = S_IRUSR | S_IXUSR | S_IWUSR;
			msgctl(localMessageQueue, IPC_SET, &msgParams);
		}
	}
	if (localMessageQueue >= 0) {
		FSChangeMessage msg;
		msg.messageType = MESSAGE_REGISTER;
		msg.userID = geteuid();
		msg.queueID = localMessageQueue;
		msg.bodyLength = 0;
		if (msgsnd(remoteMessageQueue, &msg, EMPTY_MESSAGE_LENGTH, 0) == 0) {
			while (msgrcv(localMessageQueue, &msg, EMPTY_MESSAGE_LENGTH, 0, MSG_NOERROR) < 0);
			if (msg.messageType == MESSAGE_ACKNOWLEDGE)
				registered = true;
			else
				registered = false;
			sem_post(&mutex);
			return registered;
		}
		else {
			registered = false;
			sem_post(&mutex);
			return false;
		}
	}
	else {
		registered = false;
		sem_post(&mutex);
		return false;
	}
} // end of registerWithDaemon()


bool FSLoggerClient::unregister() {
	sem_wait(&mutex);
	if (!registered) {
		sem_post(&mutex);
		return false;
	}
	FSChangeMessage msg;
	msg.messageType = MESSAGE_UNREGISTER;
	msg.userID = geteuid();
	msg.queueID = localMessageQueue;
	msg.bodyLength = 0;
	msgctl(localMessageQueue, IPC_RMID, NULL);
	localMessageQueue = -1;
	msgsnd(remoteMessageQueue, &msg, EMPTY_MESSAGE_LENGTH, 0);
	registered = false;
	sem_post(&mutex);
	return true;
} // end of unregister()


bool FSLoggerClient::isRegistered() {
	sem_wait(&mutex);
	bool result = registered;
	sem_post(&mutex);
	return result;
} // end of isRegistered()


char * FSLoggerClient::receiveNotification() {
	sem_wait(&mutex);
	if (!registered) {
		sem_post(&mutex);
		return NULL;
	}
	FSChangeMessage msg;
	char *result = NULL;
receiveNotification_START:
	int res = msgrcv(localMessageQueue, &msg, sizeof(msg), 0, MSG_NOERROR);
	while (res == -EAGAIN) {
		res = msgrcv(localMessageQueue, &msg, sizeof(msg), 0, MSG_NOERROR);
	}
	if (res <= 0) {
		sem_post(&mutex);
		return NULL;
	}
	switch (msg.messageType) {
		case MESSAGE_FSCHANGE:
			result = (char*)malloc(MAX_MESSAGE_STRING);
			memcpy(result, msg.messageString, MAX_MESSAGE_STRING);
			result[MAX_MESSAGE_STRING - 1] = 0;
			goto receiveNotification_EXIT;
			break;
		case MESSAGE_PLEASE_REFRESH:
			sem_post(&mutex);
			registerWithDaemon();
			sem_wait(&mutex);
			break;
		default:
			goto receiveNotification_START;
			break;
	}
receiveNotification_EXIT:
	sem_post(&mutex);
	return result;
} // end of receiveNotification()


