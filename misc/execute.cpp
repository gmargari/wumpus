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
 * Implementation of some useful execution functions.
 *
 * author: Stefan Buettcher
 * created: 2004-10-03
 * changed: 2007-02-20
 **/


#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "alloc.h"
#include "lockable.h"
#include "utils.h"


static long long executionCount = 0;
static long long externalTimeCount = 0;


void getExecutionStatistics(long long *executed, long long *totalTime) {
	*executed = executionCount;
	*totalTime = externalTimeCount;
} // end of getExecutionStatistics(long long*, long long*)


int executeCommand(
		const char *command, const char *param1, const char *param2, int timeout) {
	int startTime = currentTimeMillis();

	pid_t pid = fork();
	int status;
	if (pid == 0) {  // we are the child
		Lockable::disableLocking();
		fclose(stdout);
		fclose(stderr);
		execlp(command, command, param1, param2, NULL);
		exit(1);
	}
	else if (pid < 0)  // could not fork
		return -1;
	else {
		int result = waitpid(pid, &status, WNOHANG);
		int timeElapsed = 10;
		if (result == 0) {
			waitMilliSeconds(10);
			if (timeout < 0)
				timeout = 999999999;
			while ((timeElapsed < timeout) && ((result = waitpid(pid, &status, WNOHANG)) == 0)) {
				waitMilliSeconds(40);
				timeElapsed += 40;
			}
		}

		// update statistics
		executionCount++;
		externalTimeCount += (currentTimeMillis() - startTime + MILLISECONDS_PER_DAY) % MILLISECONDS_PER_DAY;

		if (result < 0) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return -1;
		}
		else if (WIFEXITED(status))
			return WEXITSTATUS(status);
		else {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return -1;
		}
	}
} // end of executeCommand(char*^3, int)


int executeCommand(const char *command,
		const char *param1, const char *param2, const char *param3, const char *param4, int timeout) {
	int startTime = currentTimeMillis();

	pid_t pid = fork();
	int status;
	if (pid == 0) {  // we are the child 
		Lockable::disableLocking();
		fclose(stdout);
		fclose(stderr);
		execlp(command, command, param1, param2, param3, param4, NULL);
		exit(1);
	}
	else if (pid < 0)  // could not fork
		return -1;
	else {
		int result = waitpid(pid, &status, WNOHANG);
		int timeElapsed = 10;
		if (result == 0) {
			waitMilliSeconds(10);
			if (timeout < 0)
				timeout = 999999999;
			while ((timeElapsed < timeout) && ((result = waitpid(pid, &status, WNOHANG)) == 0)) {
				waitMilliSeconds(40);
				timeElapsed += 40;
			}
		}

		// update statistics
		executionCount++;
		externalTimeCount += (currentTimeMillis() - startTime + MILLISECONDS_PER_DAY) % MILLISECONDS_PER_DAY;

		if (result < 0) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return -1;
		}
		else if (WIFEXITED(status))
			return WEXITSTATUS(status);
		else {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			return -1;
		}
	}
} // end of executeCommand(char*^5, int)


char * getFileType(const char *fileName, bool mime) {
	int status, pipeFDs[2];
	if (pipe(pipeFDs) != 0)
		return NULL;

	pid_t pid = fork();
	switch (pid) {
		case -1:
			perror("fork failed");
			return NULL;
		case 0:
			Lockable::disableLocking();
			close(pipeFDs[0]);
			dup2(pipeFDs[1], fileno(stdout));
			if (mime)
				execlp("file", "file", "-i", fileName, NULL);
			else
				execlp("file", "file", fileName, NULL);
			exit(1);
		default:
			close(pipeFDs[1]);
			int result = waitpid(pid, &status, WNOHANG);
			int timeout = 500;
			int timeElapsed = 10;
			if (result == 0) {
				waitMilliSeconds(10);
				while ((timeElapsed < timeout) && ((result = waitpid(pid, &status, WNOHANG)) == 0)) {
					waitMilliSeconds(10);
					timeElapsed += 10;
				}
			}
			if (result < 0) {
				fprintf(stderr, "Unable to execute file command.\n");
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				return NULL;
			}
			else if (WIFEXITED(status)) {
				char line[1024];
				int result = read(pipeFDs[0], line, sizeof(line));
				close(pipeFDs[0]);
				if (result <= 0)
					line[0] = 0;
				else if (result < sizeof(line))
					line[result] = 0;
				else
					line[sizeof(line) - 1] = 0;
				for (int i = 0; line[i] != 0; i++)
					if (line[i] == '\n')
						line[i + 1] = 0;
				for (int i = 0; line[i] != 0; i++)
					if ((line[i] == ':') && (line[i + 1] == ' '))
						return duplicateString(&line[i + 2]);
				return duplicateString(line);
			}
			else {
				fprintf(stderr, "Unable to execute file command.\n");
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				return NULL;
			}
	} // end switch (pid)

	return NULL;
} // end of getFileType(char*, bool)


