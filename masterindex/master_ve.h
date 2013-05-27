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
 * Definition of the MasterVE class, a derivation of VisibleExtents.
 *
 * author: Stefan Buettcher
 * created: 2005-03-10
 * changed: 2005-12-01
 **/


#ifndef __MASTER__MASTER_VE_H
#define __MASTER__MASTER_VE_H


#include "masterindex.h"
#include "../filemanager/securitymanager.h"


class MasterVE : public VisibleExtents {

private:

	/** Children who do the actual work for us. **/
	VisibleExtents *subVE[MasterIndex::MAX_MOUNT_COUNT];

	/** Number of non-NULL children. **/
	int subVECount;
	
public:

	MasterVE(MasterIndex *owner, uid_t userID, bool merge);

	~MasterVE();

	/**
	 * Returns the name of the file within whose range the index position "position"
	 * lies or NULL if there is no such file. Memory has to be freed by the caller.
	 **/
	char *getFileNameForOffset(offset position);

	/** Returns the document type, as defined in FilteredInputStream. **/
	int getDocumentTypeForOffset(offset position);

	/** Returns the file size of the file containing the given offset. **/
	off_t getFileSizeForOffset(offset position);

	/**
	 * Returns an ExtentList_Security instance that works on the data provided by this
	 * VisibleExtents instance. The object returned has to be deleted by the caller.
	 **/
	ExtentList *getExtentList();

	/**
	 * Returns an ExtentList instance that represents the input list filtered by
	 * list of visible extents.
	 **/
	ExtentList *restrictList(ExtentList *list);

}; // end of class MasterVE


#endif


