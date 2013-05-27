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
 * Definition of the MultiTextConnection class. MultiTextConnection is used
 * for backwards compatibility. It is a wrapper class that translates MT-style
 * queries to Wumpus queries.
 *
 * author: Stefan Buettcher
 * created: 2005-07-20
 * changed: 2009-02-01
 **/


#ifndef __DAEMONS__MULTITEXT_CONNECTION_H
#define __DAEMONS__MULTITEXT_CONNECTION_H


#include "client_connection.h"
#include "../index/index.h"


class MultiTextConnection : public ClientConnection {

protected:

	static const int WORKMODE_NORMAL = 0;
	static const int WORKMODE_COUNT = 1;
	static const int WORKMODE_ESTIMATE = 2;
	static const int WORKMODE_HISTOGRAM = 3;
	
	/** One of WORKMODE_NORMAL, WORKMODE_COUNT, WORKMODE_ESTIMATE, WORKMODE_HISTOGRAM. **/
	int workMode;

	static const int DEFAULT_LIMIT = 24;
	static const int MAX_LIMIT = 10000000;

	/** Maximum number of index extents returned. **/
	int limit;

	static const int RESPONSE_MODE_NORMAL = 1;
	static const int RESPONSE_MODE_QA = 2;
	static const int RESPONSE_MODE_QAP = 3;
	static const int RESPONSE_MODE_GET = 4;

	/** One of the above values. **/
	int responseMode;

	/**
	 * If the user is funny and sends @get queries in two lines (stupid MultiText)
	 * convention), we remember the first offset so that we can retrieve it later
	 * on when the second offset is received. -1 means: not present.
	 **/
	offset startOffsetForGetQuery, endOffsetForGetQuery;
	char fromString[32];

public:

	/**
	 * Creates a new ClientConnection object. The new object processes queries
	 * using the Index instance given by "index". Messages are sent/received via
	 * the file descriptor given by "fd". The initial user ID associated with the
	 * connection is given by "userID". It can be changed later on using the
	 * @login command.
	 **/
	MultiTextConnection(Index *index, int fd, uid_t userID);

	/** Closes the connection and deletes the object. **/
	virtual ~MultiTextConnection();

	/**
	 * Takes a MultiText-compatible query line, transforms it into a Wumpus
	 * line, calls super::processLine, and finally sends the results back
	 * to the client.
	 **/
	virtual int processLine(char *line);

protected:

	virtual int sendMessage(const char *message);

private:

	/** Tries to change the work mode, based on "command". Returns true if successful. **/
	bool changeWorkMode(char *command);

	/** @okapi old stuff --> @okapi[count=20] "<doc>".."</doc>" by "old", "stuff". **/
	int transformScorers(char *oldSequence, char *newSequence);

}; // end of MultiTextConnection


#endif

