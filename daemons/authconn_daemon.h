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
 * The AuthConnDaemon class implements a server that accepts authenticated
 * connections. An authenticated connection is established by writing a request
 * line to "/var/run/indexd.connection". Since the file has permissions 0600,
 * we know who is writing to it.
 *
 * author: Stefan Buettcher
 * created: 2004-10-08
 * changed: 2004-11-27
 **/


#ifndef __DAEMONS__AUTHCONN_DAEMON_H
#define __DAEMONS__AUTHCONN_DAEMON_H


#include "daemon.h"
#include "../index/index.h"
#include "../misc/configurator.h"


class Index;


class AuthConnDaemon : public Daemon {

private:

	/** Should be zero. Indicates if the FIFO has been created successfully. **/
	int fifo;

	/** Name of the FIFO. **/
	char *fifoName;

	/** The Index instance we belong to. **/
	Index *index;

public:

	/** Creates a new AuthConnDaemon with FIFO at default position. **/
	AuthConnDaemon(Index *index);

	/** Creates a new AuthConnDaemon with FIFO at position "connectionFIFO". **/
	AuthConnDaemon(Index *index, char *connectionFIFO);

	/** Deletes the object and removes the FIFO. **/
	virtual ~AuthConnDaemon();

	virtual void run();

private:

	void init();

	void executeQuery(char *uid, char *targetFile, char *queryString);

}; // end of class AuthConnDaemon


#endif


