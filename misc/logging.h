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
 * Header file to logging.cpp, my implementation of a multi-purpose logging
 * facility.
 *
 * author: Stefan Buettcher
 * created: 2004-09-11
 * changed: 2008-12-25
 **/


#ifndef __MISC__LOGGING_H
#define __MISC__LOGGING_H


#include <stdio.h>
#include <string>
#include "all.h"


#define LOG_DEBUG    1
#define LOG_OUTPUT   2
#define LOG_ERROR    3


void log(int logLevel, const char *logID, const char *message);

void log(int logLevel, const std::string& logID, const std::string& message);

void setLogLevel(int logLevel);

void setLogOutputStream(FILE *outStream);


#endif


