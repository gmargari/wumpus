/**
 * Copyright (C) 2005 Kevin Fong. All rights reserved.
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
 * author: Kevin Fong
 * created: 2005-10-17
 * changed: 2005-12-21
 **/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <linux/inotify.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "inotify-syscalls.h"
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include "inotifyd.h"

#include "logger.h"
#include "dirtree.h"

using namespace std;

#define __REENTRANT
#define _EVENTS (IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_ATTRIB | IN_MOVE | IN_UNMOUNT | IN_MOVE_SELF)

#define FPRINTF fprintf(debug, "[%.24s][inotifyd] ", timestamp()); fprintf

extern int errno;
Logger *logger = NULL;
bool outOfWatches = false;
bool createFlag = false;
FILE *debug = stderr;

void term_handler (int signum) {
	FPRINTF(stderr, "Signal caught %d; terminating\n", signum);
	signal(signum, SIG_DFL);
	raise(signum);
}

int main(int argc, char **argv) {
	signal(SIGINT, term_handler);
	signal(SIGTERM, term_handler);
	signal(SIGQUIT, term_handler);

	int daemon = -1;
	int watch = -1;
	int debugMsg = -1;
	bool usageHelp = false;
	bool dummy = false;

	// iterate through argv to determine flags
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			daemon = i;
		} else if (strcmp(argv[i], "-w") == 0) {
			watch = i;
		} else if (strcmp(argv[i], "-debug") == 0) {
			debugMsg = i;
		} else if (strcmp(argv[i], "-create") == 0) {
			createFlag = true;
		} else if (strcmp(argv[i], "-dummy") == 0) {
			dummy = true;
		} else if (strncmp(argv[i], "-", 1) == 0) {
			usageHelp = true;
		}
	}

	if (usageHelp) {
		printf("Usage: %s [-debug] [-create] [-dummy] [-d [FILE]] [-w path ...]\n", argv[0]);
		printf("Listen for inotify events on all local mount point and outputs to standard output\n");
		printf("\t-debug\t\toutput debug messages to standard error\n");
		printf("\t-create\t\toutput CREATE events for each file during the scan of the mount point\n");
		printf("\t-dummy\t\tforce output of an empty line once a second\n");
		printf("\t-d\t\trun as daemon, output to /tmp/inotifyd\n");
		printf("\t[output]\toutput to fifo pipe FILE\n");
		printf("\t-w\t\twatch path instead of all local mount points\n");
		printf("\n");
		printf("Requires inotify support in kernel (>2.6.13)\n");
	} else {
		char *outputFilename;
		int watchStart = -1;
		int watchEnd = -1;

		// determine if [FILE] is present
		if (daemon != -1) {
			if (daemon + 1 == argc || daemon + 1 == watch || daemon + 1 == debugMsg) {
				outputFilename = "/tmp/inotifyd";
			} else {
				outputFilename = argv[daemon+1];
			}
		} else {
			outputFilename = "";
		}

		// determine end-points of watch paths
		if (watch != -1) {
			if (!(watch + 1 == argc || watch + 1 == daemon || watch + 1 == debugMsg)) {
				watchStart = watch + 1;
				watchEnd = argc;

				if (watch < daemon) {
					watchEnd = daemon;
				}

				if (watch < debugMsg && debugMsg < daemon) {
					watchEnd = debugMsg;
				}
			}
		}

		// close the filehandle for the debug messages if -debug doesn't occur in the parameters
		if (debugMsg == -1) {
			fclose(debug);
		}

		// daemonize if -d occurs in the parameters
		if (daemon != -1) {
			daemonize();
		}

		// start the process
		init(outputFilename, watchStart, watchEnd, argv, dummy);
	}
}

void daemonize() {
	pid_t pid = fork();

	if (pid < 0) {
		FPRINTF(stderr, "Warning: unable to fork and daemonize.\n");
	} else if (pid == 0) {
		// child
		setsid();
		// write pid to file
		FILE *pidFile = fopen("inotifyd.pid", "w+");
		if (pidFile != NULL) {
			fprintf(pidFile, "%d\n", getpid());
			fflush(pidFile);
			fclose(pidFile);
		}
	} else {
		// parent
		exit(0);
	}
}

void init(char* outputFilename, int watchStart, int watchEnd, char** argv, bool dummy) {
	MountPoint *mp[128];
	memset(mp, 0, 128 * sizeof(MountPoint*));
	struct timespec sleep = {30, 0};

	FPRINTF(stderr, "Starting inotifyd; pid: %d.\n", getpid());

	// start the logger
	logger = new Logger(outputFilename);
	logger->start(dummy);

	if (watchStart != -1) {
		// start watches on the paths from the command line
		for (int i = 0; i < watchEnd - watchStart && i < 128; i++) {
			mp[i] = new MountPoint("", argv[watchStart+i]);
			mp[i]->start();
		}

		while (1) {
			// wait until program terminates
			nanosleep(&sleep, NULL);
		}
	} else {
		// start the mtab watcher
		MtabWatch *mtab = new MtabWatch();
		int count;
		mtab->start();

		while (1) {
			// scan the mtab file
			char** path = getmtab(count);
			// parse mtab data
			for (int i = 0; i < count; i+=2) {
				// compare to all currently mounted
				bool watched = false;
				int unused = -1;
				for (int j = 0; j < 128; j++) {
					if (mp[j] != NULL && strcmp(mp[j]->mountPointName,path[i+1]) == 0 && mp[j]->valid == true) {
						// currently being watched
						watched = true;
					} else if (mp[j] != NULL && mp[j]->valid == false) {
						// unmounted -- delete and clear
						delete mp[j];
						mp[j] = NULL;
					} else if (mp[j] == NULL && unused == -1) {
						unused = j;
					}
				}

				if (!watched) {
					if (unused == -1) {
						// exceeding 128 mount points -- probably shouldn't happen
						FPRINTF(stderr, "Unable to watch %s\n", path[i+1]);
					} else {
						// start watch on mount point
						mp[unused] = new MountPoint(path[i], path[i+1]);
						mp[unused]->start();
					}
				}
				free((void*) path[i]);
				free((void*) path[i+1]);
			}
			free((void*) path);

			// wait for changes to /etc/mtab
			mtab->waitForChange();
		}
	}
}

void *startScan(void* mount) {
	MountPoint *mp = (MountPoint*) mount;

	// scan directory structure
	mp->scanMount();
	pthread_exit(NULL);
}

void *startThread(void* mount) {
	MountPoint *mp = (MountPoint*) mount;

	// start directory scanner thread
	pthread_create(&(mp->scanThread), NULL, startScan, mp);
	pthread_detach(mp->scanThread);

	// start watching file descriptor for data from inotify
	mp->watchFileDescriptor();

	// mark mountpoint as no longer used and exit
	mp->valid = false;
	pthread_exit(NULL);
}

MountPoint::MountPoint(char* device, char* mount) {
	valid = true;
	fd = -1;
	maxWD = -1;

	// copy name of mount point
	mountPointName = (char*) malloc((strlen(mount) + 1) * sizeof(char));
	strcpy(mountPointName, mount);
	// copy name of device name
	deviceName = (char*) malloc((strlen(device) + 1) * sizeof(char));
	strcpy(deviceName, device);

	// create a directory tree for storing names
	tree = new DirectoryTree();
}

MountPoint::~MountPoint() {
	// free pointers
	free((void*) deviceName);
	free((void*) mountPointName);

	delete tree;
}

void MountPoint::start() {
	pthread_create(&watchThread, NULL, startThread, this);
	pthread_detach(watchThread);
}

// read events from the inotify file descriptor and send event in fschange format to the logger
void MountPoint::watchFileDescriptor() {
	inotify_event *event;
	int ret;
	int length;

	char buffer[16384];
	char *output;
	char *msg;
	char *fromName = NULL;

	Event *moveData = NULL;

	int buffer_i;
	pollfd fdp;

	// wait for file descriptor to be set
	struct timespec sleep = {1, 0};
	while (fd == -1) {
		nanosleep(&sleep, NULL);
	}

	if (fd < 0) {
		// invalid fd
		return;
	}

	FPRINTF(stderr, "Starting watch of %s\n", mountPointName);

	while (1) {
		// poll inotify file descriptor
		fdp.fd = fd;
		fdp.events = POLLIN;
		ret = poll(&fdp, 1, 100);

		if (ret < 0) {
			// error
			break;
		}

		if (fdp.revents == POLLNVAL) {
			// file descriptor has closed -- exit loop
			break;
		} else if (fdp.revents == 0) {
			// timeout -- no data on the file descriptor
			continue;
		}

		// read event(s) from descriptor
		length = read(fd, buffer, sizeof(buffer));

		// array index
		buffer_i = 0;
		event = (inotify_event*) &buffer;

		while (length - buffer_i > 0) {
			// read event from array
			event = (inotify_event*) &buffer[buffer_i];
			switch (event->mask) {
				case IN_CLOSE_WRITE:
					output = "WRITE";
					break;
				case IN_CREATE:
					output = "CREATE";
					break;
				case IN_DELETE:
					output = "UNLINK";
					break;
				case IN_ATTRIB:
				case (IN_ISDIR | IN_ATTRIB):
					output = "CHMOD";
					break;
				case IN_MOVED_FROM:
					// store name of file before move
					storeFromEvent(event, moveData);
					output = NULL;
					break;
				case IN_MOVED_TO: {
					// retrieve name stored in IN_MOVED_FROM
					Event *currentEvent = storeToEvent(event, moveData);
					if (currentEvent != NULL) {
						fromName = getFileName(currentEvent->from);
						output = "RENAME";
					} else {
						FPRINTF(stderr, "%s\n", "Move to without move from");
						output = NULL;
					}
					break;
				}
				case (IN_ISDIR | IN_CREATE):
					directoryEvent(event);
					output = "MKDIR";
					break;
				case (IN_ISDIR | IN_DELETE):
					output = "RMDIR";
					break;
				case (IN_ISDIR | IN_MOVED_FROM):
					// store name of directory before move
					storeFromEvent(event, moveData);
					output = NULL;
					break;
				case (IN_ISDIR | IN_MOVED_TO): {
					// store name of directory after move
					Event *currentEvent = storeToEvent(event, moveData);
					if (currentEvent == NULL) {
						FPRINTF(stderr, "%s\n", "Move to without move from");
					}
					output = NULL;
					break;
				}
				case (IN_MOVE_SELF): {
					// retrieve name of directory before and after
					output = NULL;
					Entry* entry = tree->getEntry(event->wd);
					if (entry == NULL) {
						FPRINTF(stderr, "%s\n", "Invalid directory entry");
					} else {
						Event *currentEvent = getEvent(entry->parent, entry->name, moveData);
						if (currentEvent != NULL && currentEvent->from != NULL && currentEvent->to != NULL) {
							fromName = getFileName(currentEvent->from);
							tree->set(event->wd, currentEvent->to->wd, currentEvent->to->name);
							output = "RENAME";
						}
					}
					break;
				}
				case (IN_DELETE_SELF):
					directoryEvent(event);
					if (event->wd == 1) {
						// first watch descriptor is removed -- no more events can be read
						output = "UMOUNT";
						close(fd);
					} else {
						output = NULL;
					}
					break;
				case (IN_UNMOUNT):
					// close file descriptor -- unmount event
					FPRINTF(stderr, "Unmount on %s\n", mountPointName);
					output = "UMOUNT";
					close(fd);
					break;
				case (IN_Q_OVERFLOW):
					// print message on error
					FPRINTF(stderr, "Overflow in event queue on %s\n", mountPointName);
					output = NULL;
					break;
				default:
					if (!(event->mask & IN_IGNORED)) {
						// FPRINTF(stderr, "%x %d %d %s\n", event->mask, event->cookie, event->wd, (event->len ? event->name : ""));
					}
					if (event->mask & IN_UNMOUNT != 0) {
						// close file descriptor -- unmount event
						FPRINTF(stderr, "Unmount on %s\n", mountPointName);
						output = "UMOUNT";
						close(fd);
					} else {
						output = NULL;
					}
					break;
			}

			if (output != NULL) {
				// get filename from event
				char *filename = getFileName(event);
				int msgLen = strlen(output) + strlen(filename) + 3;
				if (fromName != NULL) {
					msgLen += strlen(fromName) + 1;
				}

				msg = (char*) malloc(sizeof(char) * msgLen);

				// generate the FSCHANGE formatted event
				if (fromName != NULL) {
					sprintf(msg, "%s\t%s\t%s\n", output, fromName, filename);
					free((void*) fromName);
					fromName = NULL;
				} else {
					sprintf(msg, "%s\t%s\n", output, filename);
				}

				// enqueue in logger
				logger->enqueue(msg);
				free((void*) filename);
			}
			// FPRINTF(stderr, "%x %d %d %s\n", event->mask, event->cookie, event->wd, (event->len ? event->name : ""));
			buffer_i += (sizeof(struct inotify_event) + sizeof(char) * event->len);
		}
		clearEvent(moveData);
	}
	FPRINTF(stderr, "Ending watch of %s\n", mountPointName);
}

// generate a string containing the filename and path
// ** char* returned is alloc'd and must be free'd **
char* MountPoint::getFileName(inotify_event *event) {
	char *filename;

	// get path to file from the directory tree
	char *directoryName = tree->get(event->wd);

	if (event->len) {
		int fnLength = strlen(directoryName) + event->len + 2;
		filename = (char*) malloc(sizeof(char) * fnLength);
		strcpy(filename, directoryName);
		strcat(filename, "/");
		strncat(filename, event->name, event->len);
	} else {
		filename = (char*) malloc(sizeof(char) * (strlen(directoryName) + 1));
		strcpy(filename, directoryName);
	}

	return filename;
}

// store cookie and *i_event in &list
void storeFromEvent(inotify_event *i_event, Event* &list) {
	Event *event = (Event*) malloc(sizeof(Event));
	memset(event, 0, sizeof(Event));

	event->cookie = i_event->cookie;
	event->from = i_event;
	
	event->next = list;
	list = event;
}

// store cookie and *i_event in &list
Event* storeToEvent(inotify_event *i_event, Event *list) {
	while (list != NULL && list->cookie != i_event->cookie) {
		list = list->next;
	}

	if (list != NULL) {
		list->to = i_event;
	}

	return list;
}

// retrieve event using the watch descriptor and directory name
Event* getEvent(int wd, char *name, Event *list) {
	while (list != NULL && list->from != NULL && (list->from->wd != wd || strcmp(name, list->from->name) != 0)) {
		list = list->next;
	}

	return list;
}

// clear the list
void clearEvent(Event* &list) {
	Event *toFree;
	while (list != NULL) {
		toFree = list;
		list = list->next;
		free((void*) toFree);
	}
}

// process an event against a directory that affects the directory tree
int MountPoint::directoryEvent(inotify_event *event) {
	char *directoryName;
	char *name;
	int parent;

	switch (event->mask) {
		case (IN_ISDIR | IN_CREATE):
			// get full path of the directory created
			directoryName = tree->get(event->wd);	
			name = (char*) malloc((strlen(directoryName) + event->len + 2) * sizeof(char));
			memset(name, 0, sizeof(name));
			strcpy(name, directoryName);
			strcat(name, "/");
			strncat(name, event->name, event->len);

			// scan directory
			scanDirectory(name, event->name, event->wd, 1);
			free((void*) name);

			break;
		case (IN_DELETE_SELF):
			// remove entry from the directory tree data
			tree->remove(event->wd);
			break;
		default:
			break;
	}
}

// set up inotify file descriptor
// and start the recursively scanning of directories
void MountPoint::scanMount() {
	char *path = mountPointName;
	fd = inotify_init();

	if (fd < 0) {
		// error -- can't open inotify file descriptor
		FPRINTF(stderr, "Unable to create inotify file descriptor for %s\n", mountPointName);
	} else {
		// open directory
		DIR* dir_p = opendir(path);
		int dir_fd;
		int err;
		struct stat buf;
		char *name;
		if (dir_p != NULL) {
			dir_fd = dirfd(dir_p);
			err = fstat(dir_fd, &buf);
			if (err >= 0) {
				// store the device number of the mount point
				st_dev = buf.st_dev;
			} else {
				FPRINTF(stderr, "Unable to get st_dev from fstat for %s\n", mountPointName);
			}

			// set the name to be stored in the directory tree
			if (strcmp(path, "/") != 0) {
				name = path;
			} else {
				name = "";
			}
			close(dir_fd);

			FPRINTF(stderr, "Starting scan of %s\n", mountPointName);
			scanDirectory(path, name, -1, -1);
			FPRINTF(stderr, "Finished scan of %s\n", mountPointName);
		} else {
			// couldn't open directory
			FPRINTF(stderr, "Cannot open %s\n", mountPointName);
			close(fd);
		}
		closedir(dir_p);

		// watch directory structure for changes -- unnecessary
		/*
		int idx = 0;
		int wd;
		struct timespec sleep = {5, 0};
		while (1) {
			Entry* cDir = tree->getEntry(idx);
			if (cDir != NULL) {
				path = tree->get(idx);
				name = cDir->name;
				wd = cDir->parent;

				scanDirectory(path, name, wd, 1);
			}
			idx++;
			idx %= maxWD;
			nanosleep(&sleep, NULL);
		}
		*/
	}
}

// add a watch on '*path', and store the watch descriptor, '*name' and 'parent' in the directory tree
// and recurse on the subdirectories of *path based on 'depth'
// if depth == 0, do not recurse
// if depth == -1, recurse infinitly,
// otherwise decrement 'depth' on the recursive call.
void MountPoint::scanDirectory(char *path, char *name, int parent, int depth) {
	dirent* entry = NULL;
	DIR* dir_p = opendir(path);
	struct stat buf;
	int dir_fd;
	int err;
	int wd;

	if (dir_p != NULL) {
		dir_fd = dirfd(dir_p);
		err = fstat(dir_fd, &buf);

		if (buf.st_dev != st_dev || err < 0) {
			// directory is not on the same device
			closedir(dir_p);
			return;
		}

		wd = inotify_add_watch(fd, path, _EVENTS);

		if (wd < 0) {
			// insufficient watch descriptors
			if (!outOfWatches) {
				FPRINTF(stderr, "Insufficient watch descriptors;  increase number of descriptors in /proc/sys/fs/inotify/max_user_watches\n");
				outOfWatches = true;
			}
		} else {
			// add to directory entry
			tree->set(wd, parent, name);

			if (depth != 0) {
				// scan directory and add sub-directories to the watch
				entry = readdir(dir_p);
				while (entry != NULL) {
					if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
						// entry is a directory and is not "." nor ".."
						// generate path name of the subdirectory
						char *subdirPath = (char*) malloc((strlen(path) + strlen(entry->d_name) + 2) * sizeof(char));
						strcpy(subdirPath, path);
						strcat(subdirPath, "/");
						strcat(subdirPath, entry->d_name);

						// scan the subdirectory
						scanDirectory(subdirPath, entry->d_name, wd, (depth == -1 ? depth : depth - 1) );
						free((void*) subdirPath);
					} else if (entry->d_type == DT_REG && createFlag == true) {
						// output create event if the entry is a regular file and -create flag is invoked
						char *filename = (char*) malloc((strlen(path) + strlen(entry->d_name) + 2 + 8) * sizeof(char));
						strcpy(filename, "CREATE\t");
						strcat(filename, path);
						strcat(filename, "/");
						strcat(filename, entry->d_name);
						strcat(filename, "\n");
						logger->enqueue(filename);
					}
					entry = readdir(dir_p);
				}
			}
			closedir(dir_p);
		}
	}
	return;
}

void *startWatch(void* watcher) {
	MtabWatch *mt = (MtabWatch*) watcher;
	mt->run();
	pthread_exit(NULL);
}

MtabWatch::MtabWatch() {
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&modified, NULL);
}

MtabWatch::~MtabWatch() {
}

void MtabWatch::start() {
	pthread_create(&thread, NULL, startWatch, this);
	pthread_detach(thread);
}

// watch /etc/mtab
void MtabWatch::run() {
	inotify_event *event;
	int fd = inotify_init();
	int wd = inotify_add_watch(fd, "/etc", IN_CLOSE_WRITE);
	FPRINTF(stderr, "Starting watch of /etc/mtab\n");

	pollfd fdp;
	fdp.fd = fd;
	fdp.events = POLLIN;

	int ret;
	int length;
	char buffer[16384];
	int buffer_i;

	bool changed = false;

	while (1) {
		ret = poll(&fdp, 1, 500);

		if (ret < 0) {
			// error
			break;
		} else {
			if (fdp.revents == POLLNVAL) {
				// file descriptor closed -- inotify is no longer watching
				break;
			} else if (fdp.revents == 0) {
				if (changed) {
					// signal threads waiting on modification to /etc/mtab
					pthread_mutex_lock(&lock);
					pthread_cond_signal(&modified);
					pthread_mutex_unlock(&lock);
					changed = false;
				}
				continue;
			}

			// read
			length = read(fd, buffer, sizeof(buffer));
			// array index
			buffer_i = 0;
			event = (inotify_event*) &buffer;
			// FPRINTF(stderr, "%d\n", length);
			while (length - buffer_i > 0) {
				event = (inotify_event*) &buffer[buffer_i];
				switch (event->mask) {
					case IN_CLOSE_WRITE:
						if (event->len > 0 && strcmp(event->name, "mtab") == 0) {
							// set flag that /etc/mtab has been modified
							changed = true;
						}
						break;
					default:
						break;
				}
				buffer_i += (sizeof(struct inotify_event) + sizeof(char) * event->len);
			}
		}
	}
}

// wait for /etc/mtab to be modified
void MtabWatch::waitForChange() {
	pthread_mutex_lock(&lock);
	pthread_cond_wait(&modified, &lock);
	pthread_mutex_unlock(&lock);
}

// read from /etc/mtab
char** getmtab(int &count) {
	FILE* fd = fopen("/etc/mtab", "r");
	char line[1024], dev[1024], mp[1024], type[1024];
	int i = 0, limit = 64;
	char **ptr = (char**) malloc(limit * sizeof(char*));
	memset(line, 0, 1024);
	memset(dev, 0, 1024);
	memset(mp, 0, 1024);
	memset(type, 0, 1024);

	if (fd != NULL) {
		while (fgets(line, 1024, fd) != NULL) {
			if (sscanf(line, "%s %s %s %*s %*s %*s\n", &dev, &mp, &type) == 3) {
				if (i == limit) {
					limit = limit*2;
					ptr = (char**) realloc((void*) ptr, sizeof(char*) * limit);
				}

				if (strcmp(type, "ext3") == 0 || strcmp(type, "ext2") == 0) {
					ptr[i] = (char*) malloc(strlen(dev)+1);
					strcpy(ptr[i],dev);
					i++;
					ptr[i] = (char*) malloc(strlen(mp)+1);
					strcpy(ptr[i],mp);
					i++;
				}
			}
		}
		count = i;
		fclose(fd);
	} else {
		count = 0;
	}
	return ptr;
}

// generate a timestamp
char* timestamp() {
	time_t t;
	time(&t);
	return ctime(&t);
}
