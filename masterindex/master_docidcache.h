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


#ifndef __MASTER_DOCIDCACHE_H
#define __MASTER_DOCIDCACHE_H


#include "masterindex.h"
#include "../indexcache/docidcache.h"


class ExtentList;
class MasterIndex;


class MasterDocIdCache : public DocIdCache {

private:

	/** The MasterIndex associated with this cache. **/
	MasterIndex *owner;

public:

	/**
	 * Creates a new MasterDocIDCache instance that is associated with the given
	 * MasterIndex instance.
	 **/
	MasterDocIdCache(MasterIndex *owner);

	/** Stupid destructor. **/
	~MasterDocIdCache();

	/** Not implemented. Raises exception. **/
	virtual void addDocumentID(offset documentStart, char *id);

	/**
	 * Returns a copy of the document ID for the document that was added as the
	 * documentNumber-th document to the cache. NULL if there is no such document.
	 * Memory has to be freed by the caller.
	 **/
	virtual char *getDocumentID(offset documentStart);

	/** Not implemented. Raises exception. **/
	virtual void filterAgainstFileList(ExtentList *files);

}; // end of class MasterDocIDCache


#endif


