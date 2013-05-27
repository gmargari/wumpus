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
 * created: 2004-10-07
 * changed: 2005-03-14
 **/


#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include "daemon.h"
#include "../misc/all.h"


Daemon::Daemon() {
	status = STATUS_CREATED;
}


Daemon::~Daemon() {
	if (!stopped())
		stop();
	while (!stopped()) { }
} // end of ~Daemon()


static void * daemonStarter(void *daemon) {
	Daemon *d = (Daemon*)daemon;
	d->pid = getpid();
	d->run();
	return NULL;
} // end of daemonStarter(void*)


void Daemon::start() {
	bool mustReleaseLock = getLock();
	status = STATUS_RUNNING;
	pthread_create(&thread, NULL, daemonStarter, this);
	pthread_detach(thread);
	if (mustReleaseLock)
		releaseLock();
} // end of start()


void Daemon::killProcess() {
	kill(pid, SIGKILL);
} // end of killProcess()


void Daemon::run() {
	bool mustReleaseLock = getLock();
	status = STATUS_TERMINATED;
	if (mustReleaseLock)
		releaseLock();
} // end of run()


void Daemon::stop() {
	bool mustReleaseLock = getLock();
	if (status != STATUS_TERMINATED)
		status = STATUS_TERMINATING;
	if (mustReleaseLock)
		releaseLock();
} // end of stop()


bool Daemon::stopRequested() {
	int status;
	bool mustReleaseLock = getLock();
	status = this->status;
	if (mustReleaseLock)
		releaseLock();
	if (status == STATUS_TERMINATING)
		return true;
	else
		return false;
} // end of stopRequested()


bool Daemon::stopped() {
	int status;
	bool mustReleaseLock = getLock();
	status = this->status;
	if (mustReleaseLock)
		releaseLock();
	if (status == STATUS_CREATED)
		return false;
	else if (status == STATUS_TERMINATED)
		return true;
	else
		return false;
} // end of stopped()


