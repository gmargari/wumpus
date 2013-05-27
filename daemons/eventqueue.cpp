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
 * created: 2005-04-15
 * changed: 2005-06-28
 **/


#include <string.h>
#include <time.h>
#include "eventqueue.h"
#include "../index/index.h"
#include "../misc/all.h"
#include "../misc/stringtokenizer.h"


EventQueue::EventQueue(Index *owner) {
	assert(owner != NULL);
	this->owner = owner;
	for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
		eventQueue[i].eventID = 0;
		eventQueue[i].event = NULL;
	}
	queueLength = 0;
	nextID = 1;
} // end of EventQueue(Index*)


EventQueue::~EventQueue() {
	assert(this != NULL);
	stop();
	while (!stopped())
		waitMilliSeconds(WAIT_INTERVAL);
	assert(queueLength <= MAX_QUEUE_SIZE);
	for (int i = 0; i < queueLength; i++)
		if (eventQueue[i].event != NULL) {
			free(eventQueue[i].event);
			eventQueue[i].event = NULL;
		}
} // end of ~EventQueue()


void EventQueue::run() {
	assert(this != NULL);

	while ((!stopped()) && (!stopRequested())) {
		bool mustReleaseLock = getLock();
		time_t now = time(NULL);
		assert(owner != NULL);

		// If the queue is empty, then there is nothing we can do.
		if (queueLength <= 0) {
			if (mustReleaseLock)
				releaseLock();
			waitMilliSeconds(WAIT_FOR_NEW_EVENT_INTERVAL);
			continue;
		} // end if (queueLength <= 0)

		// Check if the head of the queue is cold enough to be touched by the Index.
		// If not, wait until it is.
		if (now <= eventQueue[0].timeStamp + HOT_POTATO_INTERVAL) {
			if (mustReleaseLock)
				releaseLock();
			waitMilliSeconds(WAIT_FOR_NEW_EVENT_INTERVAL);
			continue;
		} // end if (now <= eventQueue[0].creationTime + HOT_POTATO_INTERVAL)

		if (eventQueue[0].event != NULL) {
assert(eventQueue[0].event != NULL);
			owner->notify(eventQueue[0].event);
assert(eventQueue[0].event != NULL);
			free(eventQueue[0].event);
assert(eventQueue[0].event != NULL);
			eventQueue[0].event = NULL;
		}
		removeFromHistory(0);
		eventQueue[0] = eventQueue[queueLength - 1];
		updateHistoryAfterSwappingNodes(0, queueLength - 1);
		eventQueue[--queueLength].event = NULL;
		moveHeapNodeDown(0);

		if (mustReleaseLock)
			releaseLock();
		waitMilliSeconds(WAIT_INTERVAL);
	} // end while ((!stopped()) && (!stopRequested()))

	if (getLock()) {
		status = STATUS_TERMINATED;
		releaseLock();
	}
	else
		status = STATUS_TERMINATED;	
} // end of run()


void EventQueue::enqueue(char *event, time_t timeStamp) {
	assert(this != NULL);
	assert(owner != NULL);
	bool mustReleaseLock = getLock();

	// first, check whether shutdown has been requested; if so, return immediately
	if ((stopped()) || (stopRequested())) {
		if (mustReleaseLock)
			releaseLock();
		return;
	}

	// if the queue's maximum capacity has been reached, refuse to add new event
	if (queueLength >= MAX_QUEUE_SIZE) {
		if (mustReleaseLock)
			releaseLock();
		return;
	}

	eventQueue[queueLength] = createFileSystemEvent(event, &timeStamp);
	for (int i = 0; i < QUEUE_HISTORY_SIZE - 1; i++)
		history[i] = history[i + 1];
	history[QUEUE_HISTORY_SIZE - 1] = queueLength;
	moveHeapNodeUp(queueLength);
	queueLength++;

	assert(queueLength <= MAX_QUEUE_SIZE);

	if (mustReleaseLock)
		releaseLock();
} // end of enqueue(char*)


int EventQueue::getQueueLength() {
	assert(this != NULL);
	bool mustReleaseLock = getLock();
	int result = queueLength;
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getQueueLength()


int EventQueue::stringToEventType(char *string) {
	if (strcmp(string, "WRITE") == 0)
		return EVENT_WRITE;
	if (strcmp(string, "UNLINK") == 0)
		return EVENT_UNLINK;
	if (strcmp(string, "CREATE") == 0)
		return EVENT_CREATE;
	if (strcmp(string, "RENAME") == 0)
		return EVENT_RENAME;
	if (strcmp(string, "TRUNCATE") == 0)
		return EVENT_TRUNCATE;
	return EVENT_UNKNOWN;
} // end of stringToEventType(char*)


void EventQueue::tryToReduceEvents() {
	assert(this != NULL);
	assert(owner != NULL);
	bool mustReleaseLock = getLock();

	int qhs = QUEUE_HISTORY_SIZE;
	if ((queueLength < 2) || (history[qhs - 1] < 0)) {
		if (mustReleaseLock)
			releaseLock();
		return;
	}

	// extract event type and full path from the event that was last added to the queue
	int lastEvent = history[qhs - 1];
	if (eventQueue[lastEvent].event == NULL) {
		if (mustReleaseLock)
			releaseLock();
		return;
	}
	StringTokenizer *tok = new StringTokenizer(eventQueue[lastEvent].event, "\t");
	int eventType = stringToEventType(tok->getNext());
	char *path1 = duplicateString(tok->getNext());
	char *path2 = duplicateString(tok->getNext());
	delete tok;

	// event reduction is currently only supported for some events; if
	// the event is not in that set, return
	if ((eventType != EVENT_WRITE) && (eventType != EVENT_UNLINK) &&
	    (eventType != EVENT_CREATE) && (eventType != EVENT_RENAME) &&
	    (eventType != EVENT_TRUNCATE)) {
		if (path1 != NULL)
			free(path1);
		if (path2 != NULL)
			free(path2);
		if (mustReleaseLock)
			releaseLock();
		return;
	} // end if (...)

	bool reduced = true;
	while ((reduced) && (queueLength >= 2)) {
		reduced = false;
		for (int i = QUEUE_HISTORY_SIZE - 2; i >= 0; i--) {
			int event = history[i];
			if (event < 0)
				continue;
			tok = new StringTokenizer(eventQueue[event].event, "\t");
			int oldEventType = stringToEventType(tok->getNext());
			char *oldPath1 = tok->getNext();
			char *oldPath2 = tok->getNext();
			if (oldEventType == EVENT_RENAME) {
				if ((strcmp(oldPath1, path1) == 0) || (strcmp(oldPath2, path1) == 0)) {
					delete tok;
					break;
				}
			}
			bool refersToSameFile = ((path1 != NULL) && (oldPath1 != NULL));
			if (refersToSameFile)
				refersToSameFile = (strcmp(path1, oldPath1) == 0);
			delete tok;
			if (refersToSameFile) {
				bool removeEvent = false;
				if ((oldEventType == EVENT_CREATE) && (eventType == EVENT_WRITE))
					removeEvent = true;
				if ((oldEventType == EVENT_CREATE) && (eventType == EVENT_TRUNCATE))
					removeEvent = true;
				if (eventType == EVENT_UNLINK) {
					if ((oldEventType == EVENT_WRITE) || (oldEventType == EVENT_CREATE) ||
					    (oldEventType == EVENT_TRUNCATE))
						removeEvent = true;
				}

				// remove the earlier event from the queue, if told so by the reduction rules
				if (removeEvent) {
					free(eventQueue[event].event);
					eventQueue[event].event = NULL;
					removeFromHistory(event);
					eventQueue[event] = eventQueue[queueLength - 1];
					updateHistoryAfterSwappingNodes(event, queueLength - 1);
					queueLength--;
					moveHeapNodeDown(event);
					reduced = true;
				} // end if (reduced)

				break;
			}
		}
	} // end while (reduced)
	
	if (path1 != NULL)
		free(path1);
	if (path2 != NULL)
		free(path2);
	if (mustReleaseLock)
		releaseLock();
} // end of tryToReduceEvents()


void EventQueue::removeFromHistory(int node) {
	for (int i = 0; i < QUEUE_HISTORY_SIZE; i++)
		if (history[i] == node) {
			for (int k = i; k >= 1; k--)
				history[k] = history[k - 1];
			history[0] = -1;
			return;
		}
} // end of removeFromHistory(int)


void EventQueue::updateHistoryAfterSwappingNodes(int node1, int node2) {
	for (int i = 0; i < QUEUE_HISTORY_SIZE; i++) {
		if (history[i] < 0)
			continue;
		else if (history[i] == node1)
			history[i] = node2;
		else if (history[i] == node2)
			history[i] = node1;
	}
} // end of updateHistoryAfterSwappingNodes(int, int)


void EventQueue::moveHeapNodeUp(int node) {
	while (node > 0) {
		int parent = (node - 1) / 2;
		if (eventQueue[parent].timeStamp <= eventQueue[node].timeStamp)
			break;
		FileSystemEvent temp = eventQueue[node];
		eventQueue[node] = eventQueue[parent];
		eventQueue[parent] = temp;
		updateHistoryAfterSwappingNodes(node, parent);
		node = parent;
	}
} // end of moveHeapNodeUp(int)


void EventQueue::moveHeapNodeDown(int node) {
	int leftChild = 2 * node + 1;
	int rightChild = 2 * node + 2;
	while (leftChild < queueLength) {
		int child = leftChild;
		if (rightChild < queueLength)
			if (eventQueue[rightChild].timeStamp < eventQueue[leftChild].timeStamp)
				child = rightChild;
		if (eventQueue[node].timeStamp <= eventQueue[child].timeStamp)
			break;
		FileSystemEvent temp = eventQueue[node];
		eventQueue[node] = eventQueue[child];
		eventQueue[child] = temp;
		updateHistoryAfterSwappingNodes(node, child);
		node = child;
		leftChild = 2 * node + 1;
		rightChild = 2 * node + 2;
	}
} // end of moveHeapNodeDown(int)


FileSystemEvent EventQueue::createFileSystemEvent(char *event, time_t *timeStamp) {
	FileSystemEvent result;
	result.eventID = nextID++;
	result.event = duplicateString(event);
	if (timeStamp == NULL)
		result.timeStamp = time(NULL);
	else
		result.timeStamp = *timeStamp;
	return result;
} // end of createFileSystemEvent(char*, time_t*)


void EventQueue::getClassName(char *target) {
	strcpy(target, "EventQueue");
}





