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
 * created: 2005-03-10
 * changed: 2005-12-01
 **/


#include <assert.h>
#include "master_ve.h"
#include "../extentlist/extentlist.h"
#include "../filters/inputstream.h"
#include "../misc/all.h"


MasterVE::MasterVE(MasterIndex *owner, uid_t userID, bool merge) {
	bool mustReleaseLock = owner->getLock();
	subVECount = 0;
	count =  0;
	for (int i = 0; i < MasterIndex::MAX_MOUNT_COUNT; i++) {
		if ((owner->subIndexes[i] == NULL) || (owner->unmountRequested[i] >= 0))
			subVE[i] = NULL;
		else {
			subVE[i] = owner->subIndexes[i]->getVisibleExtents(userID, merge);
			subVECount++;
			count += subVE[i]->getCount();
		}
	}
	if (mustReleaseLock)
		owner->releaseLock();
} // end of MasterVE(MasterIndex)


MasterVE::~MasterVE() {
	for (int i = 0; i < MasterIndex::MAX_MOUNT_COUNT; i++)
		if (subVE[i] != NULL)
			delete subVE[i];
} // end of ~MasterVE()


char * MasterVE::getFileNameForOffset(offset position) {
	int which = position / MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if ((which < 0) || (which >= MasterIndex::MAX_MOUNT_COUNT))
		return NULL;
	position = position % MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if (subVE[which] == NULL)
		return NULL;
	else
		return subVE[which]->getFileNameForOffset(position);
} // end of getFileNameForOffset(offset)


int MasterVE::getDocumentTypeForOffset(offset position) {
	int which = position / MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if ((which < 0) || (which >= MasterIndex::MAX_MOUNT_COUNT))
		return FilteredInputStream::DOCUMENT_TYPE_UNKNOWN;
	position = position % MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if (subVE[which] == NULL)
		return FilteredInputStream::DOCUMENT_TYPE_UNKNOWN;
	else
		return subVE[which]->getDocumentTypeForOffset(position);
} // end of getDocumentTypeForOffset(offset)


off_t MasterVE::getFileSizeForOffset(offset position) {
	int which = position / MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if ((which < 0) || (which >= MasterIndex::MAX_MOUNT_COUNT))
		return -1;
	position = position % MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if (subVE[which] == NULL)
		return -1;
	else
		return subVE[which]->getDocumentTypeForOffset(position);
} // end of getFileSizeForOffset(offset)


ExtentList * MasterVE::getExtentList() {
	if (this == NULL)
		return new ExtentList_Empty();
	ExtentList **subLists = typed_malloc(ExtentList*, (subVECount > 0 ? subVECount : 1));
	offset *relativeOffs = typed_malloc(offset, (subVECount > 0 ? subVECount : 1));
	int cnt = 0;
	for (int i = 0; i < MasterIndex::MAX_MOUNT_COUNT; i++) {
		if (subVE[i] != NULL) {
			subLists[cnt] = subVE[i]->getExtentList();
			relativeOffs[cnt] = i * MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
			if (subLists[cnt]->getLength() == 0)
				delete subLists[cnt];
			else
				cnt++;
		}
	}
	assert(cnt <= subVECount);
	if (cnt == 0) {
		free(subLists);
		free(relativeOffs);
		return new ExtentList_Empty();
	}
	else
		return new ExtentList_OrderedCombination(subLists, relativeOffs, cnt);
} // end of getExtentList()


ExtentList * MasterVE::restrictList(ExtentList *list) {
	if (this == NULL)
		return list;
	else
		return new ExtentList_Containment(getExtentList(), list, false, false);
} // end of restrictList(ExtentList*)


