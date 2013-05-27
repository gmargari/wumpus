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
 * The EventQueue class manages file system events. Once an event is read
 * from the fschange or inotify interface (or whatever source), it is queued
 * and scheduled for processing. Event processing is done asynchronously, and
 * multiple events can nullify each other (for example a CREATE followed by
 * an UNLINK event).
 
 * The plans are to have EventQueue support persistent logging that is used
 * during system recovery. However, this has not been implemented yet.
 *
 * author: Stefan Buettcher
 * created: 2005-06-28
 * changedL 2005-06-28
 **/


#ifndef __DAEMONS__EVENTQUEUE_H
#define __DAEMONS__EVENTQUEUE_H


#include "daemon.h"
#include <time.h>


#define MAX_EVENT_LENGTH 2048


typedef struct {

	/**
	 * Unique ID for the event. Monotonically increasing. **/
	unsigned int eventID;

	/**
	 * Time of event creation. This is not the time the event was created inside the
	 * kernel, but the time it was put into the event queue.
	 **/
	time_t timeStamp;

	/** The event itself. In the usual fschange format. **/
	char *event;

} FileSystemEvent;


class Index;


class EventQueue : public Daemon {

public:


	/** We won't make the queue longer than this. **/
	static const int MAX_QUEUE_SIZE = 16384;

	/**
	 * How long can we look back into the past when examining event objects in
	 * the queue? This is used to nullify events (e.g., CREATE followed by UNLINK).
	 **/
	static const int QUEUE_HISTORY_SIZE = 32;

	/** General constant for all wait operations (between events, at shutdown, ...). **/
	static const int WAIT_INTERVAL = 2;

	/**
	 * When no new event is waiting to be processed, we wait this long until we
	 * look at the queue again.
	 **/
	static const int WAIT_FOR_NEW_EVENT_INTERVAL = 40;

	/** Do not process a file system event that is younger than 1 second. **/
	static const int HOT_POTATO_INTERVAL = 1;

private:

	static const int EVENT_UNKNOWN  = 0x00;
	static const int EVENT_WRITE    = 0x01;
	static const int EVENT_CREATE   = 0x02;
	static const int EVENT_UNLINK   = 0x04;
	static const int EVENT_TRUNCATE = 0x08;
	static const int EVENT_RENAME   = 0x10;

private:

	/** Index instance that the EventQueue is reporting to. **/
	Index *owner;

	/**
	 * This is a heap structure for all events in the queue. We need this in order
	 * to be able to insert events in asynchronous order into the queue.
	 **/
	FileSystemEvent eventQueue[MAX_QUEUE_SIZE];

	/** Slots allocated for the queue. **/
	int queueLength;

	/** The last QUEUE_HISTORY_SIZE events (pointers to heap positions). **/
	int history[QUEUE_HISTORY_SIZE];

	/** Used to create event IDs. **/
	unsigned int nextID;

public:

	/**
	 * Creates a new EventQueue instance. The object notifies the given Index
	 * instance ("owner") about events.
	 **/
	EventQueue(Index *owner);

	/** Boring class destructor. **/
	virtual ~EventQueue();

	/** Standard Daemon::run method. **/
	virtual void run();

	/**
	 * Adds the given event, which took place at the time described by "timeStamp",
	 * to the event queue.
	 **/
	void enqueue(char *event, time_t timeStamp);

	/** Returns the number of events in the queue. **/
	int getQueueLength();

protected:

	virtual void getClassName(char *target);

private:

	/**
	 * Creates a new FileSystemEvent object from the string description of the
	 * event ("event") and the given time stamp.
	 **/
	FileSystemEvent createFileSystemEvent(char *event, time_t *timeStamp);

	/**
	 * This method is responsible for nullifying events such as CREATE-UNLINK
	 * sequences.
	 **/
	void tryToReduceEvents();

	/** Moves the heap node up in the heap, according to the events' time stamps. **/
	void moveHeapNodeUp(int node);

	/** Moves the heap node down in the heap, according to the events' time stamps. **/
	void moveHeapNodeDown(int node);

	/**
	 * When we swap the positions of two nodes in the heap structure, we have to
	 * update the event history (last QUEUE_HISTORY_LENGTH events) so that its content
	 * reflects the new positions of those two nodes.
	 **/
	void updateHistoryAfterSwappingNodes(int node1, int node2);

	/** Removes the event in heap node "node" from the history. **/
	void removeFromHistory(int node);

	/** Returns a numerical event ID for the event string given by "string". **/
	static int stringToEventType(char *string);
	
}; // end of class EventQueue


#endif



