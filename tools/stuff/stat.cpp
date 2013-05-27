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
 * Performs a stat operation on the file given.
 *
 * author: Stefan Buettcher
 * created: 2004-10-26
 * changed: 2004-10-26
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


static char buffer[1024];

char *timestr(time_t t) {
	char *result = ctime(&t);
	strcpy(buffer, result);
	buffer[strlen(buffer) - 1] = 0;
	return buffer;
} // end of timestr(time_t)


int main(int argc, char **argv) {
	bool follow = false;
	char *fileName = NULL;
	switch (argc) {
		case 2:
			follow = false;
			fileName = argv[1];
			break;
		case 3:
			if (strcmp(argv[1], "-follow") != 0) {
				printf("Usage: stat [-follow] filename\n");
				return 1;
			}
			follow = true;
			fileName = argv[2];
			break;
		default:
			printf("Usage: stat [-follow] filename\n");
			return 1;
	}
	struct stat buf;
	int result;
	if (follow)
		result = stat(fileName, &buf);
	else
		result = lstat(fileName, &buf);
	if (result != 0) {
		perror(NULL);
		return 1;
	}
	printf("st_dev:    %i\n", (int)buf.st_dev);
	printf("st_ino:    %i\n", (int)buf.st_ino);
	printf("st_mode:   %o\n", (int)buf.st_mode);
	printf("st_nlink:  %i\n", (int)buf.st_nlink);
	printf("st_uid:    %i\n", (int)buf.st_uid);
	printf("st_gid:    %i\n", (int)buf.st_gid);
	printf("st_rdev:   %i\n", (int)buf.st_rdev);
	printf("st_size:   %i\n", (int)buf.st_size);
	printf("st_atime:  %i (%s)\n", (int)buf.st_atime, timestr(buf.st_atime));
	printf("st_mtime:  %i (%s)\n", (int)buf.st_mtime, timestr(buf.st_mtime));
	printf("st_ctime:  %i (%s)\n", (int)buf.st_ctime, timestr(buf.st_ctime));
	return 0;
} // end of main(int, char**)


