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
 * Definition of the ClientConnection class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-26
 * changed: 2009-02-01
 **/


#ifndef __DAEMONS__CLIENT_CONNECTION_H
#define __DAEMONS__CLIENT_CONNECTION_H


#include "daemon.h"
#include "conn_daemon.h"
#include "../index/index.h"


class ConnDaemon;


class ClientConnection : public Daemon {

protected:

	/** Index used to respond to queries. **/
	Index *index;

	/** Input/output socket. **/
	int fd;

	/** Who is the remote user? We need this to determine read permissions etc. **/
	uid_t userID;

	/**
	 * We try to work on every query in a separate process. This allows us to simply
	 * kill that process the very moment the client closes the connection. If
	 * "forkOnQuery" is set to false, forking is disabled, and a thread is created
	 * instead. This improves query processing latency.
	 **/
	bool forkOnQuery;

	/** Read buffer. Used to received commands from the client. **/
	char buffer[65536];

	/** Number of bytes currently in the buffer. **/
	int bufferSize;

public:

	/** Dummy constructor. **/
	ClientConnection();

	/**
	 * Creates a new ClientConnection object. The new object processes queries
	 * using the Index instance given by "index". Messages are sent/received via
	 * the file descriptor given by "fd". The initial user ID associated with the
	 * connection is given by "userID". It can be changed later on using the
	 * @login command.
	 **/
	ClientConnection(Index *index, int fd, uid_t userID);

	/** Closes the connection and deletes the object. **/
	virtual ~ClientConnection();

	/** This is the "true" constructor. To be called by all child classes. **/
	virtual void initialize(Index *index, int fd, uid_t userID);

	/**
	 * Loops until either side closes the connection. Reads commands from the
	 * socket and sends response messages back to the client.
	 **/
	virtual void run();

	/**
	 * Takes a line received from the client, processes it and returns the number
	 * of bytes sent back to the client in response to the query. If this method
	 * returns -1, this means it was unable to send a response because the con-
	 * nection has been closed. In this case, we will initiate the connection
	 * shutdown sequence. Boom!
	 **/
	virtual int processLine(char *line);

	/** Closes the socket. This forced the connection to terminate. **/
	virtual void closeSocket();

protected:

	/** Sends the given message to the client. **/
	virtual int sendMessage(const char *message);

private:

	/**
	 * Takes a username/password pair of the form "USERNAME PASSWORD" or
	 * "USERNAME\tPASSWORD". Returns true if the combination was correct. The
	 * password check is done using the wumpus.passwd file and the system
	 * /etc/shadow file (the latter only if the process has superuser privileges).
	 **/
	bool authenticate(char *userNamePassword);

	/**
	 * Waits until data have been received from the client or the connection has
	 * been closed (either by the client or by the server itself).
	 **/
	void waitForDataOrHUP();

	/** Processes a query of the format @getfile FILENAME. **/
	int processGetFileQuery(char *line);

}; // end of ClientConnection


#endif

