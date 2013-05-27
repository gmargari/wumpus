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
 * FakeIndex sits on top of an Index instance and is used by the ClientConnection
 * class to pre-parse queries without fetching any data from the index. We use
 * FakeIndex in order to avoid fetching postings for syntactically incorrect queries.
 * There is one exception, however: The cache management methods are fully
 * functional, and all messages passed to them are directly forwarded to the under-
 * lying Index instance.
 *
 * author: Stefan Buettcher
 * created: 2005-08-01
 * changed: 2007-12-17
 **/


#ifndef __INDEX__FAKEINDEX_H
#define __INDEX__FAKEINDEX_H


#include "index.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"
#include <string.h>


class FakeIndex : public Index {

private:

	/** The underlying Index instance. **/
	Index *index;

public:

	/** Creates a new FakeIndex instance sitting on top of the given Index. **/
	FakeIndex(Index *index) {
		this->index = index;
		indexType = TYPE_FAKEINDEX;
	}

	virtual ~FakeIndex() {
	}

	virtual int notify(const char *event) {
		return 0;
	}

	virtual void notifyOfAddressSpaceChange(int signum, offset start, offset end) {
	}

	virtual ExtentList *getPostings(const char *term, uid_t userID);

	virtual ExtentList *getPostings(const char *term, uid_t userID, bool fromDisk, bool fromMemory);

	virtual void getPostings(char **terms, int termCount, uid_t userID, ExtentList **results);

	virtual void addAnnotation(offset position, const char *annotation) {
	}

	virtual inline void getAnnotation(offset position, char *buffer) {
		buffer[0] = 0;
	}

	virtual inline void removeAnnotation(offset position) {
	}

	virtual inline offset getBiggestOffset() {
		return 0;
	}

	virtual inline int getDocumentType(const char *fullPath) {
		return -1;
	}

	virtual inline bool getLastIndexToTextSmallerEq(offset where,
				offset *indexPosition, off_t *filePosition) {
		return false;
	}

	virtual inline uid_t getOwner() {
		return index->getOwner();
	}

	virtual inline VisibleExtents *getVisibleExtents(uid_t userID, bool merge) {
		return NULL;
	}

	virtual inline void getDictionarySize(offset *lowerBound, offset *upperBound) {
		*lowerBound = *upperBound = 0;
	}

	virtual inline int64_t registerForUse() {
		return 1;
	}

	virtual inline int64_t registerForUse(int64_t suggestedID) {
		return suggestedID + 1;
	}

	virtual inline void deregister(int64_t id) {
	}

	virtual inline void waitForUsersToFinish() {
	}

	virtual inline void getIndexSummary(char *buffer) {
		buffer[0] = 0;
	}

	virtual inline int64_t getTimeStamp(bool withLocking) {
		return index->getTimeStamp(withLocking);
	}

	virtual inline ExtentList *getCachedList(const char *queryString) {
		return index->getCachedList(queryString);
	}

	virtual inline IndexCache *getCache() {
		return index->getCache();
	}

	virtual inline void compact() {
	}

	virtual void sync() {
	}

protected:

	virtual void getConfiguration() {
	}

	virtual void setMountPoint(const char *mountPoint) {
	}

	virtual void getClassName(char *target) {
		strcpy(target, "FakeIndex");
	}

}; // end of class FakeIndex


#endif


