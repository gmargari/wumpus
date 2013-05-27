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
 * Definition of the QueryExecutor class. QueryExecutor is used to process
 * queries in parallel. For each incoming query, a new thread is spawned.
 *
 * author: Stefan Buettcher
 * created: 2005-03-14
 * changed: 2009-02-01
 **/


#ifndef __DAEMONS__QUERY_EXECUTOR_H
#define __DAEMONS__QUERY_EXECUTOR_H


#include "../query/query.h"


typedef struct {

	Query *query;

	int outputFD;

} QuerySessionDescriptor;


class QueryExecutor {

public:

	/**
	 * Creates a new thread that processes the query given by "queryString". The
	 * results of the query will be written to "outputFD". After the query execution
	 * has finished, "outputFD" will be shutdown and closed (unless it is one of
	 * stdin, stdout, stderr).
	 * The Query instance will automatically be taken care of, and all memory will
	 * be released once the query has been processed.
	 **/
	static void executeQuery(Query *query, int outputFD);

	/**
	 * Calls shutdown and close for "fd" (unless it is stdin, stdout, or stderr).
	 **/
	static void shutdownAndClose(int fd);

}; // end of class QueryExecutor


#endif


