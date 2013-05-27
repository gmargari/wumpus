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
 * Definition of the OnDiskIndex class.
 *
 * author: Stefan Buettcher
 * created: 2005-11-22
 * changed: 2005-11-22
 **/


#ifndef __INDEX__ONDISK_INDEX_H
#define __INDEX__ONDISK_INDEX_H


#include "index_types.h"
#include "../extentlist/extentlist.h"
#include "../misc/lockable.h"


class OnDiskIndex : public Lockable {

public:

	OnDiskIndex() { }

	virtual ~OnDiskIndex() { }

	virtual void addPostings(const char *term, offset *postings, int count) = 0;

	virtual void addPostings(const char *term, byte *compressedPostings,
			int byteLength, int count, offset first, offset last) = 0;

	virtual ExtentList *getPostings(const char *term) = 0;

	virtual int64_t getTermCount() = 0;

	virtual int64_t getByteSize() = 0;

	virtual int64_t getPostingCount() = 0;

	virtual char *getFileName() = 0;

}; // end of class OnDiskIndex


#endif


