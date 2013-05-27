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
 * author: Stefan Buettcher
 * created: 2004-09-11
 * changed: 2008-12-25
 **/


#include <stdio.h>
#include <string.h>
#include <time.h>
#include "logging.h"
#include "alloc.h"


static int sLogLevel = LOG_OUTPUT;

static FILE *sOutputStream = stderr;


void log(int logLevel, const char *logID, const char *message) {
	if (logLevel < sLogLevel)
		return;
	char buffer[256];
	time_t now = time(NULL);
	ctime_r(&now, buffer);
	if (buffer[strlen(buffer) - 1] == '\n')
		buffer[strlen(buffer) - 1] = 0;
	switch (logLevel) {
		case LOG_DEBUG:
			fprintf(sOutputStream, "(DEBUG) [%s] [%s] %s\n", logID, buffer, message);
			break;
		case LOG_OUTPUT:
			fprintf(sOutputStream, "(OUTPUT) [%s] [%s] %s\n", logID, buffer, message);
			break;
		case LOG_ERROR:
			fprintf(sOutputStream, "(ERROR) [%s] [%s] %s\n", logID, buffer, message);
			break;
	}
} // end of log(int, int, char*)


void log(int logLevel, const std::string& logID, const std::string& message) {
	log(logLevel, logID.c_str(), message.c_str());
}


void setLogLevel(int logLevel) {
	sLogLevel = logLevel;
}


void setLogOutputStream(FILE *outputStream) {
	sOutputStream = outputStream;
}


