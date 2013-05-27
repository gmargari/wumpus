#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "logger.h"

#define __REENTRANT

#define FPRINTF fprintf(debug, "[%.24s][logger] ", timestamp()); fprintf

extern int errno;
extern FILE* debug;
extern char *timestamp();

Logger::Logger(char* outputFileName) {
	head = 0;
	tail = 0;
	buffer = (char**) (malloc(sizeof(char*) * BUFFER_SIZE));

	memset(buffer, 0, sizeof(char*) * BUFFER_SIZE);
	memset(used, 0, sizeof(int) * BUFFER_SIZE);

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&empty, NULL);

	if (strcmp(outputFileName, "") != 0) {
		// output to a filehandle
		struct stat outputFileStat;
		int err = stat(outputFileName, &outputFileStat);

		if (err == -1) {
			// file does not exist
			if (errno == ENOENT) {
				// create fifo pipe
				err = mkfifo(outputFileName, 00644);
				if (err == 0) {
					err = stat(outputFileName, &outputFileStat);
				} else {
					// can't create pipe
					FPRINTF(debug, "Cannot create %s; writing to standard output\n", outputFileName);
					outputFD = stdout;
					outputFN = NULL;
					return;
				}
			} else {
				// can't stat file
				FPRINTF(debug, "Cannot stat %s; writing to standard output\n", outputFileName);
				outputFD = stdout;
				outputFN = NULL;
				return;
			}
		}

		// open file
		int log = open(outputFileName, O_RDWR);
		if (log >= 0) {
			// create file descriptor
			FILE *fd = fdopen(log, "r+");
			if (fd != NULL) {
				outputFD = fd;
				outputFN = outputFileName;
				if (!(outputFileStat.st_mode & S_IFIFO)) {
					// warn if file is not a fifo pipe
					FPRINTF(debug, "Warning: %s is not a fifo pipe\n", outputFileName);
				}
			} else {
				// can't create file descriptor
				FPRINTF(debug, "Cannot open file descriptor for %s; writing to standard output\n");
				outputFD = stdout;
				outputFN = NULL;
			}
		} else {
			// can't open file
			FPRINTF(debug, "Cannot open file %s for writing; writing to standard output\n", outputFileName);
			outputFD = stdout;
			outputFN = NULL;
		}
	} else {
		// set output file descriptor to standard out
		outputFD = stdout;
		outputFN = NULL;	
	}
}

Logger::~Logger() {
	// delete all stored events
	for (int i = 0; i < BUFFER_SIZE; i++) {
		if (used[i]) {
			free((void*) buffer[i]);
		}
	}
	free((void*) buffer);
}

// store pointer to string in the queue
// ** atomic operation on the queue **
void Logger::enqueue(char* ptr) {
	pthread_mutex_lock(&lock);
	buffer[tail] = ptr;
	used[tail] = 1;

	if (head == tail) {
		// wake the printer
		pthread_cond_signal(&empty);
	}

	tail = (tail + 1) % BUFFER_SIZE;

	// check for wrap around
	if (tail == head) {
		if (used[head]) {
			free((void*) buffer[head]);
			used[head] = 0;
		}
		head = (head + 1) % BUFFER_SIZE;
	}
	pthread_mutex_unlock(&lock);
}

void *startLogger(void* logger) {
	Logger *log = (Logger*) logger;

	// start thread
	log->run();
	pthread_exit(NULL);
}

void *startDummy(void* logger) {
	Logger *log = (Logger*) logger;

	// start thread
	log->dummyThreadRun();
	pthread_exit(NULL);
}

void Logger::start(bool dummy) {
	pthread_create(&thread, NULL, startLogger, this);
	pthread_detach(thread);
	if (dummy) {
		pthread_create(&dummyThread, NULL, startDummy, this);
		pthread_detach(dummyThread);
	}
}

void Logger::run() {
	char *ptr = NULL;
	while (1) {
		// ** atomic operation on the queue **
		// acquire lock on the queue
		pthread_mutex_lock(&lock);

		while (head == tail) {
			// nothing on the queue -- wait until someone enqueues
			pthread_cond_wait(&empty, &lock);
		}

		// dequeue a string
		ptr = buffer[head];
		used[head] = 0;
		head = (head + 1) % BUFFER_SIZE;

		// release lock on the queue
		pthread_mutex_unlock(&lock);

		if (ptr != NULL) {
			// print string to file descriptor
			// ** blocking operation if no reader on the file descriptor **
			fprintf(outputFD, ptr);
			fflush(outputFD);
			free((void*) ptr);
		}
	}
}

void Logger::dummyThreadRun() {
	struct timespec sleep = {1, 0};
	char *emptyLine;
	while (1) {
		emptyLine = (char*) malloc(sizeof(char)*2);
		strcpy(emptyLine, "\n");
		enqueue(emptyLine);
		nanosleep(&sleep, NULL);
	}
}
	
