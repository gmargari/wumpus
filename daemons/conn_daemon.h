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
 * The ConnDaemon class is responsible for TCP connections to the index server.
 * TCP connections can be used for unauthenticated queries (UID == NOBODY).
 *
 * author: Stefan Buettcher
 * created: 2004-10-08
 * changed: 2009-02-01
 **/


#ifndef __DAEMONS__CONN_DAEMON_H
#define __DAEMONS__CONN_DAEMON_H


#include "daemon.h"
#include "client_connection.h"
#include "../index/index.h"
#include "../misc/lockable.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


class ClientConnection;


class ConnDaemon : public Daemon {

public:

	static const int DEFAULT_MAX_TCP_CONNECTIONS = 4;

	/** "ConnDaemon". **/
	static const char *LOG_ID;

private:

	Index *index;

	int listenPort;

	int listenSocket;

	int MAX_TCP_CONNECTIONS;

	ClientConnection *activeConnections[32];

	int activeConnectionCount;

public:

	/** Creates a new daemon listening on port "listenPort". **/
	ConnDaemon(Index *index, int listenPort);

	virtual ~ConnDaemon();

	virtual void run();

	/**
	 * Adds the connection given by "cc" to the daemon's list of active connections.
	 * This is useful because it allows us to kill active connection when the daemon
	 * is stopped. Returns "false" iff maximum number of connections has been reached.
	 **/
	bool addActiveConnection(ClientConnection *cc);

	void killAllActiveConnections();

private:

	void init();

	void executeQuery(int tcpConnection, char *queryString);

}; // end of class ConnDaemon


#endif


