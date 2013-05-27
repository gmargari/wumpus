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
 * The Daemon interface defines the basic behaviour of all our daemons.
 * The most important daemons are the ConnDaemon, the AuthConnDaemon,
 * and the FileSysDaemon.
 *
 * author: Stefan Buettcher
 * created: 2004-10-07
 * changed: 2005-03-14
 **/


#ifndef __DAEMONS__DAEMON_H
#define __DAEMONS__DAEMON_H


#include "../misc/lockable.h"
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>


class Daemon : public Lockable {

protected:

	static const int STATUS_CREATED = 0;
	static const int STATUS_RUNNING = 1;
	static const int STATUS_TERMINATING = 2;
	static const int STATUS_TERMINATED = 3;

	/** One of the above. **/
	int status;

	/** Thread used by this Daemon. **/
	pthread_t thread;

public:

	/** Process ID of the thread (assuming that threads are processes...). **/
	pid_t pid;

public:

	Daemon();

	virtual ~Daemon();

	virtual void start();

	virtual void run();

	virtual void stop();

	virtual bool stopRequested();

	virtual bool stopped();

	virtual void killProcess();

}; // end of class Daemon


#endif


