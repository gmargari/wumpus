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
 * Header file to execute.cpp, which contains some useful functions I like to
 * call when executing external commands.
 *
 * author: Stefan Buettcher
 * created: 2004-10-03
 * changed: 2007-03-01
 **/
	  

#ifndef __MISC__EXECUTE_H
#define __MISC__EXECUTE_H


/**
 * Executes command "command" with parameters "param1" and "param2".
 * Returns the 8 least significant bits of the child's exit code or -1 if
 * something went wrong.
 * "timeout" can be used to specify a timeout in milliseconds. If after this
 * threshold the child process has not returned, it is killed, and we return
 * -1. "timeout < 0" means: no timeout.
 **/
int executeCommand(
		const char *command, const char *param1, const char *param2, int timeout);

int executeCommand(
		const char *command, const char *param1, const char *param2,
		const char *param3, const char *param4, int timeout);

/**
 * Returns the file type of the given file (or NULL if it cannot be determined)
 * by calling the "file" command. If "mime" is true, the MIME type is returned
 * instead of the flat file type.
 **/
char * getFileType(const char *fileName, bool mime);


void getExecutionStatistics(long long *executed, long long *totalTime);


#endif


