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
 * created: 2005-02-20
 * changed: 2005-12-01
 **/


#include <pwd.h>
#ifndef __APPLE__
#include <shadow.h>
#endif
#include <string.h>
#include "securitymanager.h"
#include "extentlist_security.h"
#include "../index/index.h"
#include "../index/postinglist.h"
#include "../misc/all.h"


VisibleExtents::VisibleExtents() {
	fm = NULL;
	extents = NULL;
	usageCounter = 0;
} // end of VisibleExtents()


VisibleExtents::VisibleExtents(FileManager *fm, VisibleExtent *extents, int count) {
	this->fm = fm;
	this->extents = extents;
	if (extents == NULL)
		extents = typed_malloc(VisibleExtent, 1);
	this->extents = extents;
	this->count = count;
	usageCounter = 0;
} // end of VisibleExtents(FileManager *fm, VisibleExtent*, int)


VisibleExtents::~VisibleExtents() {
	assert(usageCounter == 0);
	if (extents != NULL)
		free(extents);
} // end of ~VisibleExtents()


int VisibleExtents::getIdForOffset(offset position) {
	if (count == 0)
		return -1;
	if ((position < extents[0].startOffset) ||
	    (position >= extents[count - 1].startOffset + extents[count - 1].tokenCount))
		return -1;
	int lower = 0;
	int upper = count - 1;
	while (upper > lower) {
		int middle = (upper + lower + 1) / 2;
		if (extents[middle].startOffset > position)
			upper = middle - 1;
		else
			lower = middle;
	}
	if ((extents[lower].startOffset > position) ||
	    (extents[lower].startOffset + extents[lower].tokenCount <= position))
		return -1;
	return lower;
} // end of getIdForOffset(offset)


char * VisibleExtents::getFileNameForOffset(offset position) {
	int id = getIdForOffset(position);
	if (id < 0)
		return NULL;
	else
		return fm->getFilePath(extents[id].fileID);
} // end of getFileNameForOffset(offset)


int VisibleExtents::getDocumentTypeForOffset(offset position) {
	int id = getIdForOffset(position);
	if (id < 0)
		return FilteredInputStream::DOCUMENT_TYPE_UNKNOWN;
	else
		return extents[id].documentType;
} // end of getDocumentTypeForOffset(offset)


off_t VisibleExtents::getFileSizeForOffset(offset position) {
	IndexedINodeOnDisk iiod;
	int id = getIdForOffset(position);
	if (id < 0)
		return -1;
	else if (fm->getINodeInfo(extents[id].fileID, &iiod))
		return iiod.fileSize;
	else
		return -1;
} // end of getFileSizeForOffset(offset)


ExtentList * VisibleExtents::getExtentList() {
	if (this == NULL)
		return new ExtentList_Empty();
	else
		return new ExtentList_Security(this);
} // end of getExtentList()


int VisibleExtents::getCount() {
	return count;
}


ExtentList * VisibleExtents::restrictList(ExtentList *list) {
	if (this == NULL)
		return list;
	else
		return new ExtentList_Containment(new ExtentList_Security(this), list, false, false);
} // end of restrictList(ExtentList*)


SecurityManager::SecurityManager(FileManager *fm) {
	this->fm = fm;
} // end of SecurityManager(FileManager*)


SecurityManager::~SecurityManager() {
} // end of ~SecurityManager()


VisibleExtents * SecurityManager::getVisibleExtents(uid_t userID, bool merge) {
	int cnt;
	VisibleExtent *result = fm->getVisibleFileExtents(userID, &cnt);
	if ((merge) && (cnt > 1)) {
		int inPos = 1;
		int outPos = 1;
		offset currentEnd = result[0].startOffset + result[0].tokenCount;
		while (inPos < cnt) {
			if (result[inPos].startOffset < currentEnd + FILE_GRANULARITY) {
				if ((result[inPos].tokenCount >= 2000000000) ||
				    (result[outPos - 1].tokenCount >= 2000000000))
					result[outPos++] = result[inPos];
				else {
					unsigned int newTokenCount =
						result[inPos].startOffset - result[outPos - 1].startOffset;
					result[outPos - 1].tokenCount = newTokenCount + result[inPos].tokenCount;
				}
			}
			else
				result[outPos++] = result[inPos];
			inPos++;
			currentEnd = result[outPos - 1].startOffset + result[outPos - 1].tokenCount;
		}
		if (outPos < 0.9 * cnt)
			typed_realloc(VisibleExtent, result, outPos + 1);
		cnt = outPos;
	}
	return new VisibleExtents(fm, result, cnt);
} // end of getVisibleExtents(uid_t, bool)


ExtentList * SecurityManager::getVisibleExtentStarts(uid_t userID) {
	int cnt;
	VisibleExtent *result = fm->getVisibleFileExtents(userID, &cnt);
	if (cnt == 0) {
		if (result != NULL)
			free(result);
		return new ExtentList_Empty();
	}
	offset *resultList = typed_malloc(offset, cnt);
	for (int i = 0; i < cnt; i++)
		resultList[i] = result[i].startOffset;
	free(result);
	return new PostingList(resultList, cnt, false, true);
} // end of getVisibleExtentStarts(uid_t)


ExtentList * SecurityManager::getVisibleExtentEnds(uid_t userID) {
	int cnt;
	VisibleExtent *result = fm->getVisibleFileExtents(userID, &cnt);
	if (cnt == 0) {
		if (result != NULL)
			free(result);
		return new ExtentList_Empty();
	}
	offset *resultList = typed_malloc(offset, cnt);
	for (int i = 0; i < cnt; i++)
		resultList[i] = (result[i].startOffset + result[i].tokenCount) - 1;
	free(result);
	return new PostingList(resultList, cnt, false, true);
} // end of getVisibleExtentEnds(uid_t)


uid_t SecurityManager::authenticate(char *userName, char *password) {
#ifdef __APPLE__
	// lckpwdf etc. are not supported by MacOS.
	return (uid_t)-1;
#else
	if ((userName == NULL) || (password == NULL))
		return (uid_t)-1;
	uid_t result = (uid_t)-1;
	lckpwdf();
	setpwent();
	setspent();
	struct passwd *passwdEntry = getpwnam(userName);
	struct spwd *shadowEntry = getspnam(userName);
	if ((shadowEntry != NULL) && (passwdEntry != NULL)) {
		char *plain = password;
		char *encrypted = shadowEntry->sp_pwdp;
		if (strcmp(crypt(plain, encrypted), encrypted) == 0)
			result = passwdEntry->pw_uid;
	}
	endpwent();
	endspent();
	ulckpwdf();
	return result;
#endif
} // end of authenticate(char*, char*)



