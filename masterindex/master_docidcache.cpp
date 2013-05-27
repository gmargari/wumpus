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
 * created: 2005-09-20
 * changed: 2005-09-20
 **/


#include "master_docidcache.h"
#include "masterindex.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"


MasterDocIdCache::MasterDocIdCache(MasterIndex *owner) {
	this->owner = owner;
	fileHandle = -1;
} // end of MasterDocIdCache(MasterIndex*)


MasterDocIdCache::~MasterDocIdCache() {
} // end of ~MasterDocIdCache()


void MasterDocIdCache::addDocumentID(offset documentStart, char *id) {
	assert("Not implemented" == NULL);
}


char * MasterDocIdCache::getDocumentID(offset documentStart) {
	char *result = NULL;
	bool mustReleaseLock = owner->getLock();
	int which = documentStart / MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if ((which < 0) || (which >= MasterIndex::MAX_MOUNT_COUNT))
		goto getDocumentID_EXIT;
	documentStart = documentStart % MasterIndex::MAX_INDEX_RANGE_PER_INDEX;
	if (owner->subIndexes[which] == NULL)
		goto getDocumentID_EXIT;
	if (owner->subIndexes[which]->documentIDs == NULL)
		goto getDocumentID_EXIT;
	result = owner->subIndexes[which]->documentIDs->getDocumentID(documentStart);
getDocumentID_EXIT:
	if (mustReleaseLock)
		owner->releaseLock();
	return result;
} // end of getDocumentID(offset)


void MasterDocIdCache::filterAgainstFileList(ExtentList *files) {
	assert("Not implemented" == NULL);
}



