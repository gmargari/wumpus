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
 * This is a modified version of umount that waits for the indexing service
 * to finish its work.
 *
 * author: Stefan Buettcher
 * created: 2005-03-15
 * changed: 2005-03-15
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


/** Every time we go to bed, we do this for 100 milliseconds. **/
#define WAIT_PERIOD 100

/** How long are we willing to wait in total? (in milliseconds) **/
#define TOTAL_WAIT_TIME 5000


static void waitMilliSeconds(int ms) {
	if (ms <= 1)
		return;
	struct timespec t1, t2;
	t1.tv_sec = (ms / 1000);
	t1.tv_nsec = (ms % 1000) * 1000000;
	int result = nanosleep(&t1, &t2);
	if (result < 0) {
		t1 = t2;
		nanosleep(&t1, &t2);
	}
} // end of waitMilliSeconds(int)


static int umount(char *param) {
	static char *command = "/bin/umount";
	pid_t pid = fork();
	int status;
	if (pid == 0) {  // we are the child
		fclose(stdout);
		fclose(stderr);
		execlp(command, command, param, NULL);
	}
	else if (pid < 0) {  // could not fork
		perror("Unable to fork");
		exit(1);
	}
	else // we are the parent: wait for umount to return
		waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	else
		return -1;
} // end of umount(char*)


int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "This is a modified version of umount. It takes exactly one parameter.\n");
		fprintf(stderr, "Try /bin/umount if you want to do fancier stuff.\n");
		return 1;
	}
	int result = umount(argv[1]);
	if (result != 0) {
		printf("Filesystem busy. Waiting for processes to release files...\n");
		waitMilliSeconds(WAIT_PERIOD);
		for (int i = 0; i < TOTAL_WAIT_TIME / WAIT_PERIOD; i++)
			if ((result = umount(argv[1])) == 0)
				break;
		if (result < 0)
			printf("umount failed. Open files were not closed in time.\n");
		else
			printf("Filesystem unmounted.\n");
	}
	return 0;
} // end of main(int, char**)


