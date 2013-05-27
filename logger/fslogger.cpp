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
 * The fschange logger is a daemon process running with super-user privileges.
 * It reads data from /proc/fschange (in case the fschange module has been loaded;
 * if it has not been loaded, the process refuses to start). User processes may
 * register with the logging service in order to get information about file
 * system changes.
 *
 * author: Stefan Buettcher
 * created: 2005-03-04
 * changed: 2005-03-10
 **/


#include <fcntl.h>
#include <grp.h>
#include <linux/sysctl.h>
#include <linux/unistd.h>
#include <pthread.h>
#include <pwd.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "fslogger.h"
#include "../misc/all.h"


/** PID of the parent process. **/
static pid_t originalPID;

/** Effective UID of this process. **/
static uid_t euid;

/** The fschange /proc file handle. **/
static int procFD;

/** Handle to the daemon's message queue. **/
static int messageQueue;

/** ID of the thread that reads from the /proc file. **/
static pthread_t readThread;

/** PID of the reading thread. **/
static pid_t readThreadPID;

/**
 * This is set to true when we receive the request to shutdown on the message
 * queue. The thread that reads from the /proc file will react to this.
 **/
static bool terminated = false;

/** We manage the clients in a simple array of RegisteredClient objects. **/
static const int MAX_CLIENT_COUNT = 64;
static RegisteredClient clients[MAX_CLIENT_COUNT * 2];
static int clientCount = 0;

/**
 * We have a mutex to prevent the two threads from accessing and modifying
 * the internal data (registered processes) at the same time.
 **/
static sem_t mutex;

/**
 * Message object with pre-filled member variables. Used to send refresh
 * requests.
 **/
static FSChangeMessage refreshRequest;

/** Similar to "refreshRequest", but for ACK messages. **/
static FSChangeMessage acknowledgement;

/** Three guys that are read when somebody asks for our status. **/
static int eventsReceived = 0;
static int messagesReceived = 0;
static int messagesSent = 0;


/** Tries to open the /proc file. Returns true if successful, false otherwise. **/
static bool openProcFile() {
	procFD = open(FSCHANGE_PROC_FILE, O_RDONLY);
	if (procFD >= 0)
		return true;
	else
		return false;
} // end of openProcFile()


/**
 * Tries to create a message queue with key "MSG_QUEUE_KEY". Returns true if
 * successful, false otherwise.
 **/
static bool createMessageQueue() {
	int accessMode = S_IRUSR | S_IXUSR | S_IWUSR | S_IWGRP | S_IWOTH;
	messageQueue = msgget(MSG_QUEUE_KEY, IPC_CREAT | IPC_EXCL | accessMode);
	while (messageQueue == 0) {
		msgctl(messageQueue, IPC_RMID, NULL);
		messageQueue = msgget(MSG_QUEUE_KEY, IPC_CREAT | IPC_EXCL | accessMode);
	}
	if (messageQueue >= 0)
		return true;
	else
		return false;
} // end of createMessageQueue()


_syscall1(int, _sysctl, struct __sysctl_args *, args);


static bool adjustMaxMessageQueueCount() {
	// read KERNEL.MSGMNI (maximum number of active message queues)
	int sysctlPath[] = { CTL_KERN, KERN_MSGMNI };
	int value;
	size_t valueSize = sizeof(value);
	struct __sysctl_args args = { sysctlPath, 2, &value, &valueSize, 0, 0 };
	int result = _sysctl(&args);
	if ((result != 0) || (valueSize != sizeof(value)))
		return false;
	if (value < 64) {
		value = 64;
		struct __sysctl_args args2 = { sysctlPath, 2, 0, 0, &value, valueSize };
		result = _sysctl(&args2);
		if (result != 0)
			return false;
	}
	return true;
} // end of adjustMaxMessageQueueCount()


static void printSyntax() {
	fprintf(stderr, "File System Change Logging Service (for the fschange kernel patch)\n\n");
	fprintf(stderr, "Syntax:  fslogger  (start|stop|restart|status)\n\n");
	exit(1);
} // end of printSyntax()


static void dieWithErrorMessage(char *msg) {
	fprintf(stderr, "%s: ", msg);
	perror(NULL);
	if (getpid() == originalPID)
		fprintf(stderr, "Process terminated.\n");
	exit(1);
} // end of dieWithErrorMessage(char*)


static int gidComparator(const void *a, const void *b) {
	gid_t *x = (gid_t*)a;
	gid_t *y = (gid_t*)y;
	if (*x < *y)
		return -1;
	else if (*x > *y)
		return +1;
	else
		return 0;
} // end of gidComparator(const void*, const void*)


static void getGroupsForUser(uid_t userID, gid_t *groups, int *groupCount) {
	int result = 0;
	struct passwd *pwd = getpwuid(userID);
	if (pwd == NULL) {
		*groupCount = 0;
		return;
	}
	char *userName = pwd->pw_name;
	struct group *groupData;
	while ((groupData = getgrent()) != NULL) {
		for (int i = 0; groupData->gr_mem[i] != NULL; i++) {
			if (strcmp(userName, groupData->gr_mem[i]) == 0) {
				groups[result++] = groupData->gr_gid;
				break;
			}
		}
		if (result >= 32)
			break;
	}
	endgrent();
	qsort(groups, result, sizeof(gid_t), gidComparator);
	*groupCount = result;
} // end of getGroupsForUser(uid_t, gid_t*, int*)


static void processEvent(int elementCount, char **elements, char *fullString) {
	int stringLength = strlen(fullString);
	if (stringLength >= MAX_MESSAGE_STRING)
		return;
	if (elementCount < 2)
		return;

	sem_wait(&mutex);

	// prepare message object
	FSChangeMessage message;
	message.messageType = MESSAGE_FSCHANGE;
	message.userID = euid;
	message.queueID = messageQueue;
	message.bodyLength = stringLength;
	strcpy(message.messageString, fullString);
	int messageLength = EMPTY_MESSAGE_LENGTH + stringLength;

	// check file permissions based on the event type
	struct stat buf;
	bool everybody = false;
	uid_t owner1 = 0, owner2 = 0;
	if ((strcmp(elements[0], "MOUNT") == 0) || (strcmp(elements[0], "UMOUNT") == 0))
		everybody = true;
	else {
		char *directory = (char*)malloc(strlen(elements[1]) + 4);
		strcpy(directory, elements[1]);
		char *ptr = &directory[strlen(directory) - 1];
		if (*ptr == '/')
			*(ptr--) = 0;
		while ((ptr > directory) && (*ptr != '/'))
			*(ptr--) = 0;
		if ((strcmp(elements[0], "WRITE") == 0) || (strcmp(elements[0], "TRUNCATE") == 0)) {
			if (stat(directory, &buf) == 0)
				owner1 = buf.st_uid;
			if (stat(elements[2], &buf) == 0)
				owner2 = buf.st_uid;
		}
		else if ((strcmp(elements[0], "CHMOD") == 0) || (strcmp(elements[0], "CREATE") == 0)) {
			if (stat(directory, &buf) == 0)
				owner1 = buf.st_uid;
			if (stat(elements[2], &buf) == 0)
				owner2 = buf.st_uid;
		}
		else if ((strcmp(elements[0], "MKDIR") == 0) || (strcmp(elements[0], "CHOWN") == 0)) {
			if (stat(directory, &buf) == 0)
				owner1 = buf.st_uid;
			if (stat(elements[2], &buf) == 0)
				owner2 = buf.st_uid;
		}
		else if (strcmp(elements[0], "RENAME") == 0) {
			if (stat(directory, &buf) == 0)
				owner1 = buf.st_uid;
			if (stat(elements[2], &buf) == 0)
				owner2 = buf.st_uid;
		}
		else if ((strcmp(elements[0], "UNLINK") == 0) || (strcmp(elements[0], "RMDIR") == 0)) {
			if (stat(directory, &buf) == 0)
				owner1 = owner2 = buf.st_uid;
		}
		free(directory);
	} // end else [not "MOUNT", not "UMOUNT"]

	time_t now = time(NULL);
	for (int i = 0; i < clientCount; i++) {
		if (now > clients[i].lastRefresh + INACTIVITY_THRESHOLD) {
			if (clients[i].refreshRequestSent) {
				msgctl(clients[i].messageQueue, IPC_RMID, NULL);
				clients[i--] = clients[--clientCount];
			}
			else {
				msgsnd(clients[i].messageQueue, &refreshRequest, EMPTY_MESSAGE_LENGTH, IPC_NOWAIT);
				messagesSent++;
				clients[i].refreshRequestSent = true;
			}
		}
		if ((everybody) || (clients[i].userID == 0) ||
		    (clients[i].userID == owner1) || (clients[i].userID == owner2)) {
			msgsnd(clients[i].messageQueue, &message, messageLength, IPC_NOWAIT);
			messagesSent++;
		}
	}
	sem_post(&mutex);
} // end of processEvent(int, char**)


/**
 * Enters an infinite loop in which it reads stuff from the /proc file
 * and passes it on to all registered clients.
 **/
static void * readFromProcFile(void *data) {
	char line[8192];
	FILE *file = fdopen(procFD, "r");
	char *eventElements[32];
	readThreadPID = getpid();
	if (file == NULL)
		terminated = true;
	else {
		while (!terminated) {
			if (fgets(line, 8190, file) != NULL) {
				int len = strlen(line);
				if (len < 3)
					continue;
				while ((len > 2) && (line[len - 1] == '\n'))
					line[--len] = 0;
				int elemCount = 0;
				StringTokenizer *tok = new StringTokenizer(line, "\t");
				while (tok->hasNext())
					eventElements[elemCount++] = tok->nextToken();
				processEvent(elemCount, eventElements, line);
				delete tok;
				eventsReceived++;
			}
			else {
				fclose(file);
				file = fopen(FSCHANGE_PROC_FILE, "r");
				if (file == NULL)
					terminated = true;
			}
		}
	}

	// send a message to the other thread so that it wakes up
	FSChangeMessage message;
	message.messageType = MESSAGE_ACKNOWLEDGE;
	message.userID = (uid_t)-1;
	msgsnd(messageQueue, (struct msgbuf*)&message, sizeof(message), IPC_NOWAIT);
	messagesSent++;
	return NULL;
} // end of readFromProcFile(void*)


static void processRegister(FSChangeMessage *message) {
	sem_wait(&mutex);
	time_t now = time(NULL);
	bool found = false;
	for (int i = 0; i < clientCount; i++) {
		if ((clients[i].userID == message->userID) && (clients[i].messageQueue == message->queueID)) {
			clients[i].lastRefresh = now;
			clients[i].refreshRequestSent = false;
			getGroupsForUser(message->userID, clients[clientCount].groups,
					&clients[clientCount].groupCount);
			msgsnd(clients[i].messageQueue, &acknowledgement, EMPTY_MESSAGE_LENGTH, IPC_NOWAIT);
			messagesSent++;
			found = true;
		}
		else if (now > clients[i].lastRefresh + INACTIVITY_THRESHOLD) {
			if (clients[i].refreshRequestSent) {
				msgctl(clients[i].messageQueue, IPC_RMID, NULL);
				clients[i--] = clients[--clientCount];
			}
			else {
				msgsnd(clients[i].messageQueue, &refreshRequest, EMPTY_MESSAGE_LENGTH, IPC_NOWAIT);
				messagesSent++;
				clients[i].refreshRequestSent = true;
			}
		}
	}
	if (found) {
		sem_post(&mutex);
		return;
	}
	if (clientCount < MAX_CLIENT_COUNT) {
		clients[clientCount].userID = message->userID;
		clients[clientCount].messageQueue = message->queueID;
		clients[clientCount].lastRefresh = now;
		clients[clientCount].refreshRequestSent = false;
		getGroupsForUser(message->userID, clients[clientCount].groups,
				&clients[clientCount].groupCount);
		clientCount++;
		msgsnd(clients[clientCount - 1].messageQueue, &acknowledgement,
				EMPTY_MESSAGE_LENGTH, IPC_NOWAIT);
		messagesSent++;
	}
	sem_post(&mutex);
} // end of processRegister(FSChangeMessage*)


static void processUnregister(FSChangeMessage *message) {
	sem_wait(&mutex);
	time_t now = time(NULL);
	for (int i = 0; i < clientCount; i++) {
		if ((clients[i].userID == message->userID) && (clients[i].messageQueue == message->queueID))
			clients[i--] = clients[--clientCount];
		else if (now > clients[i].lastRefresh + INACTIVITY_THRESHOLD) {
			if (clients[i].refreshRequestSent) {
				msgctl(clients[i].messageQueue, IPC_RMID, NULL);
				clients[i--] = clients[--clientCount];
			}
			else {
				msgsnd(clients[i].messageQueue, &refreshRequest, EMPTY_MESSAGE_LENGTH, IPC_NOWAIT);
				messagesSent++;
				clients[i].refreshRequestSent = true;
			}
		}
	}
	sem_post(&mutex);
} // end of processUnregister(FSChangeMessage*)


/** Sends a status message to the message queue given by "targetQueue". **/
static void sendStatusMessage(int targetQueue) {
	FSChangeMessage msg;
	msg.messageType = MESSAGE_STATUS;
	msg.userID = euid;
	msg.queueID = messageQueue;
	int bodyLength = 0;
	bodyLength +=
		sprintf(&msg.messageString[bodyLength], "  Events received: %d\n", eventsReceived);
	bodyLength +=
		sprintf(&msg.messageString[bodyLength], "  Messages received: %d\n", messagesReceived);
	bodyLength +=
		sprintf(&msg.messageString[bodyLength], "  Messages sent: %d\n", messagesSent);
	bodyLength +=
		sprintf(&msg.messageString[bodyLength], "  Registered clients: %d\n", clientCount);
	msg.bodyLength = bodyLength + 1;
	msgsnd(targetQueue, &msg, EMPTY_MESSAGE_LENGTH + bodyLength + 1, 0);
	messagesSent++;
} // end of sendStatusMessage(int)


/**
 * Enters an infinite loop in which it waits for messages from users and
 * processes them.
 **/
static void waitForMessages() {
	uid_t uid;

	// adjust the queue permissions and increase the size of the receive buffer
	struct msqid_ds msgParams;
	msgctl(messageQueue, IPC_STAT, &msgParams);
	msgParams.msg_perm.mode = S_IRUSR | S_IXUSR | S_IWUSR | S_IWGRP | S_IWOTH;
	msgParams.msg_qbytes = 512;
	msgctl(messageQueue, IPC_SET, &msgParams);

	// wait for incoming messages
	FSChangeMessage message;
	while (!terminated) {
		int result = msgrcv(messageQueue, &message, EMPTY_MESSAGE_LENGTH, 0, MSG_NOERROR);
		if (result < 0)
			continue;
		messagesReceived++;

		switch (message.messageType) {
			case MESSAGE_SHUTDOWN:
				uid = message.userID;
				// verify that the SHUTDOWN message comes from an authorized user
				if ((uid == 0) || (uid == euid)) {
					if (message.queueID == messageQueue) {
						// check if the user has changed the queue permissions; if so, this
						// means that he has the appropriate rights and is authorized to
						// send SHUTDOWN messages
						if (msgctl(messageQueue, IPC_STAT, &msgParams) == 0)							
							if (msgParams.msg_perm.mode == S_IRUSR | S_IXUSR | S_IWUSR)
								goto waitForMessages_EXIT;
					}
				}
				break;
			case MESSAGE_STATUS:
				uid = message.userID;
				// verify that the STATUS message comes from an authorized user
				if ((uid == 0) || (uid == euid))
					if (msgctl(message.queueID, IPC_STAT, &msgParams) == 0)
						if (msgParams.msg_perm.mode == S_IRUSR | S_IXUSR | S_IWUSR)
							sendStatusMessage(message.queueID);
				break;
			case MESSAGE_ACKNOWLEDGE:
				// ignore acknowledgement messages
				break;
			case MESSAGE_REGISTER:
				// make sure that the process who sent this message is actually authorized
				// to do this (compare message.userID with queue's uid)
				if (msgctl(message.queueID, IPC_STAT, &msgParams) == 0) {
					if ((msgParams.msg_perm.uid == message.userID) &&
					    (msgParams.msg_perm.cuid == message.userID) &&
					    (msgParams.msg_perm.mode == (S_IRUSR | S_IXUSR | S_IWUSR)))
						processRegister(&message);
				}
				break;
			case MESSAGE_UNREGISTER:
				// authentication for UNREGISTER messages: if the message queue has been
				// deleted, we assume that in fact the calling process is not interested
				// in notifications any more
				if (msgctl(message.queueID, IPC_STAT, &msgParams) != 0)
					processUnregister(&message);
				break;
		}
	} // end while (true)

waitForMessages_EXIT:

	// if we get here, that means that somebody has requested termination;
	// send an acknowledgement and terminate the daemon (both threads)

	msgsnd(messageQueue, &acknowledgement, EMPTY_MESSAGE_LENGTH, 0);
	messagesSent++;

	terminated = true;
	kill(readThreadPID, SIGINT);
} // end of waitForMessages()


/**
 * Starts the logging daemon. Makes sure there is no instance of the daemon
 * running so far.
 **/
static void start() {
	if (!openProcFile())
		dieWithErrorMessage("Unable to open proc file (" FSCHANGE_PROC_FILE ")");
	if (!adjustMaxMessageQueueCount())
		dieWithErrorMessage("Unable to increase the number of message queues");
	if (!createMessageQueue())
		dieWithErrorMessage("Unable to create message queue");

	// file has been opened, message queue has been created; now, fork into two
	// processes: one will read stuff from the /proc file, the other will listen
	// for user messages on the message queue
	pid_t pid = fork();
	if (pid < 0)
		dieWithErrorMessage("Unable to create new process");
	else if (pid == 0) {
		// pid == 0 means that we are the child process: continue here
		setsid();
		chdir("/");
		umask(0);
		// initialize the mutex
		sem_init(&mutex, 0, 1);
		// create a helper thread that will read data from the /proc file
		pthread_create(&readThread, NULL, readFromProcFile, NULL);
		pthread_detach(readThread);
		refreshRequest.messageType = MESSAGE_PLEASE_REFRESH;
		refreshRequest.userID = euid;
		refreshRequest.queueID = messageQueue;
		refreshRequest.bodyLength = 0;
		acknowledgement.messageType = MESSAGE_ACKNOWLEDGE;
		acknowledgement.userID = euid;
		acknowledgement.queueID = messageQueue;
		acknowledgement.bodyLength = 0;
		waitForMessages();
	}
	else {
		printf("Daemon process started.\n");
#if 0
		// enable this piece of code if you don't want the parent process to terminate
		char line[1024];
		printf("Press [ENTER] to terminate.\n");
		fgets(line, 1022, stdin);
		msgctl(messageQueue, IPC_RMID, NULL);
		terminated = true;
		kill(pid, SIGINT);
#endif
	}
} // end of start()


/** Stops a running logging daemon (if there is one). **/
static void stop() {
	FSChangeMessage message;
	messageQueue = msgget(MSG_QUEUE_KEY, S_IRUSR | S_IXUSR | S_IWUSR);
	if (messageQueue < 0)
		dieWithErrorMessage("Unable to connect to daemon (msgget failed)");

	// change message queue permissions in such a way that only we and the
	// daemon may use it any longer
	struct msqid_ds msgParams;
	msgctl(messageQueue, IPC_STAT, &msgParams);
	msgParams.msg_perm.mode = S_IRUSR | S_IXUSR | S_IWUSR;
	msgctl(messageQueue, IPC_SET, &msgParams);

	// initialize message
	message.messageType = MESSAGE_SHUTDOWN;
	message.userID = getuid();
	message.queueID = messageQueue;
	message.bodyLength = 0;

	// send message to daemon
	int result = msgsnd(messageQueue, (struct msgbuf*)&message, EMPTY_MESSAGE_LENGTH, 0);
	if (result < 0)
		dieWithErrorMessage("Unable to connect to daemon (msgsnd failed)");

	// sleep a bit and then read the response from daemon
	waitMilliSeconds(500);
	while (true) {
		msgrcv(messageQueue, &message, EMPTY_MESSAGE_LENGTH, 0, MSG_NOERROR);
		if ((message.messageType == MESSAGE_ACKNOWLEDGE) &&
		    (message.userID == msgParams.msg_perm.uid)) {
			// we got an ACK from the daemon, which means it has terminated; we may
			// now delete the message queue
			msgctl(messageQueue, IPC_RMID, &msgParams);
			printf("Daemon process stopped.\n");
			return;
		}
	}

} // end of stop()


/**
 * Stops the currently running daemon and starts a new instance. If there is no
 * daemon running right now, restart() behaves the same way as start().
 **/
static void restart() {
	// we do this by creating splitting into two processes: the first will
	// call stop(), the second waits until the first has terminated and then
	// call start()
	pid_t pid;
	pid = fork();
	if (pid < 0)
		dieWithErrorMessage("Unable to restart daemon (fork failed)");
	else if (pid == 0)
		stop();
	else {
		int status;
		while (waitpid(pid, &status, 0) != pid);
		start();
	}
} // end of restart()


/**
 * If a logging daemon is currently running, we print some status information
 * about it. If not, we return with an error message.
 **/
static void status() {
	FSChangeMessage msg;
	messageQueue = msgget(MSG_QUEUE_KEY, S_IRUSR | S_IXUSR | S_IWUSR);
	if (messageQueue < 0) {
		printf("No daemon process running.\n");
		return;
	}
	int localQueue = msgget(IPC_PRIVATE, S_IRUSR | S_IXUSR | S_IWUSR);
	if (localQueue < 0)
		dieWithErrorMessage("Unable to connect to daemon (msgget failed)");
	msg.messageType = MESSAGE_STATUS;
	msg.userID = euid;
	msg.queueID = localQueue;
	msg.bodyLength = 0;
	if (msgsnd(messageQueue, &msg, EMPTY_MESSAGE_LENGTH, 0) != 0)
		dieWithErrorMessage("Unable to connect to daemon (msgsnd failed)");
	while (msgrcv(localQueue, &msg, sizeof(msg), 0, MSG_NOERROR) < 0);
	if (msg.messageType == MESSAGE_STATUS) {
		msgctl(localQueue, IPC_RMID, NULL);
		printf("Daemon process running.\n");
		printf("%s", msg.messageString);
	}
	else {
		fprintf(stderr, "Received garbage message from daemon.");
		exit(1);
	}
} // end of status()


int main(int argc, char **argv) {
	originalPID = getpid();
	euid = geteuid();
	if (argc != 2)
		printSyntax();
	if (strcmp(argv[1], "start") == 0)
		start();
	else if (strcmp(argv[1], "stop") == 0)
		stop();
	else if (strcmp(argv[1], "restart") == 0)
		restart();
	else if (strcmp(argv[1], "status") == 0)
		status();
	else {
		fprintf(stderr, "Illegal parameter value: %s\n", argv[1]);
		return 1;
	}
	return 0;
} // end of main(int, char**)


