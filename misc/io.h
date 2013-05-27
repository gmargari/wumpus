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
 * I/O helper functions.
 *
 * author: Stefan Buettcher
 * created: 2005-02-09
 * changed: 2009-02-01
 **/


#ifndef __MISC__IO_H
#define __MISC__IO_H


#include <unistd.h>
#include <sys/types.h>
#include "all.h"


void getReadWriteStatistics(long long *bytesRead, long long *bytesWritten);

#define forced_read(fd, buf, count) forced_read5(fd, buf, count, __FILE__, __LINE__)

#define forced_write(fd, buf, count) forced_write5(fd, buf, count, __FILE__, __LINE__)
	
/**
 * Reads "count" bytes from "fd" into "buf". Repeats the call to "read" in case
 * of interruption. Returns the number of bytes read.
 **/
int forced_read3(int fd, void *buf, size_t count);

int forced_read5(int fd, void *buf, size_t count, const char *file, int line);


/**
 * Writes "count" bytes from "buf" to "fd". Repeats the call to "write" in case
 * of interruption. Returns the number of bytes written.
 **/
int forced_write3(int fd, const void *buf, size_t count);

int forced_write5(int fd, const void *buf, size_t count, const char *file, int line);


#define forced_ftruncate(fd, length) forced_ftruncate4(fd, length, __FILE__, __LINE__)

void forced_ftruncate4(int fd, off_t length, const char *file, int line);


#endif


