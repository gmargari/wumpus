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
 * Header file for the fschange logging service.
 *
 * author: Stefan Buettcher
 * created: 2005-03-04
 * changed: 2005-03-05
 **/


#ifndef __FSLOGGER__H
#define __FSLOGGER__H


#include <sys/types.h>
#include "fslogger_client.h"


/** /proc file that tells us what is going on. **/
#define FSCHANGE_PROC_FILE "/proc/fschange"


/**
 * After INACTIVITY_THRESHOLD seconds of inactivity (failure to re-register
 * with the logging daemon, we send a request to refresh registration to the
 * client.
 **/
static const int INACTIVITY_THRESHOLD = 25;

/**
 * After sending the request to refresh registration, we give the client
 * RESPOND_TO_REFRESH_REQUEST_THRESHOLD seconds to do the refresh. When the
 * timer runs out, we remove the client from the list.
 **/
static const int RESPOND_TO_REFRESH_REQUEST_THRESHOLD = 5;


typedef struct {

	/** UID of the process that registered for notification. **/
	uid_t userID;

	/** Number of groups that this guy is a member of. **/
	int groupCount;

	/** Up to 32 different groups for each client. **/
	gid_t groups[32];

	/** The message queue that the client receives notifications on. **/
	int messageQueue;

	/**
	 * Time when the client issued the last refresh command. We use this to
	 * remove clients after a certain period of inactivity.
	 **/
	time_t lastRefresh;

	/**
	 * When INACTIVITY_THRESHOLD is reached, we send a request to refresh
	 * registration to the client and set "refreshRequestSent = true". When
	 * INACTIVITY_THRESHOLD is reached a second time, we remove the client
	 * from the list of registered clients. If the client refreshes the
	 * registration before the timer runs out, we set "refreshRequestSent = false"
	 * again.
	 **/
	bool refreshRequestSent;

} RegisteredClient;


#endif


