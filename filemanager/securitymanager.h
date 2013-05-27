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
 * The SecurityManager is tightly couple with the FileManager. It is only
 * used for compatibility with my earlier class structure. Processes call the
 * getVisibleExtents method in order to obtain a list of all visible extents
 * for a user.
 *
 * author: Stefan Buettcher
 * created: 2005-02-20
 * changed: 2005-12-01
 **/


#ifndef __FILEMANAGER__SECURITYMANAGER_H
#define __FILEMANAGER__SECURITYMANAGER_H


#include <sys/types.h>
#include "filemanager.h"
#include "data_structures.h"
#include "../extentlist/extentlist.h"
#include "../index/index_types.h"
#include "../misc/all.h"


class VisibleExtents {

	friend class ExtentList_Security;

protected:
	
	/** FileManager instance that gave us the information. **/
	FileManager *fm;

	/** Number of elements in the "extents" list. **/
	int count;

	/** Sequence of offsets. **/
	VisibleExtent *extents;

	/** Tells us how may objects are currently using this VisibleExtents instance. **/
	int usageCounter;

public:

	VisibleExtents();

	VisibleExtents(FileManager *fm, VisibleExtent *extents, int count);

	virtual ~VisibleExtents();

	/**
	 * Returns the name of the file within whose range the index position "position"
	 * lies or NULL if there is no such file. Memory has to be freed by the caller.
	 **/
	virtual char *getFileNameForOffset(offset position);

	/**
	 * Returns the document type of the file given by "position". Document types
	 * are defined in the FilteredInputStream class.
	 **/
	virtual int getDocumentTypeForOffset(offset position);

	/** Returns the file size of the file containing the given offset. **/
	virtual off_t getFileSizeForOffset(offset position);

	/**
	 * Returns an ExtentList_Security instance that works on the data provided by this
	 * VisibleExtents instance. The object returned has to be deleted by the caller.
	 **/
	virtual ExtentList *getExtentList();

	/** Returns the number of extents in this list. **/
	virtual int getCount();

	/**
	 * Returns an ExtentList instance that represents the input list filtered by
	 * list of visible extents.
	 **/
	virtual ExtentList *restrictList(ExtentList *list);

	virtual FileManager *getFileManager() { return (this != NULL ? fm : NULL); }

private:

	/** Returns the internal ID value for the file containing the given index address. **/
	int getIdForOffset(offset position);

}; // end of class VisibleExtents


class SecurityManager {

private:

	FileManager *fm;

public:

	/**
	 * Creates a new SecurityManager instance that takes the security information
	 * from the FileManager given by "fm".
	 **/
	SecurityManager(FileManager *fm);

	/** Class destructor. **/
	~SecurityManager();

	/**
	 * Returns a list of all index extents that may be searched by the user given
	 * by "userID". "merge" tells the SecurityManager whether it is OK for the
	 * calling process to get a list in which adjacent extents have been merged
	 * in order to save some space.
	 **/
	VisibleExtents *getVisibleExtents(uid_t userID, bool merge);

	/**
	 * Returns an ExtentList instance that represents the start offsets of all
	 * files searchable by the user "userID".
	 **/
	ExtentList *getVisibleExtentStarts(uid_t userID);

	/**
	 * Returns an ExtentList instance that represents the end offsets of all
	 * files searchable by the user "userID".
	 **/
	ExtentList *getVisibleExtentEnds(uid_t userID);

	/**
	 * Runs an authentication process for the user given by "userName". Returns
	 * a valid user ID iff the password supplied matches the user's system password.
	 * (uid_t)-1 otherwise.
	 **/
	static uid_t authenticate(char *userName, char *password);

}; // end of class SecurityManager


#endif


