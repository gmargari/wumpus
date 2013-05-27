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
 * The IndexIterator2 class is used to iterate over the content of an
 * on-disk index of the CompactIndex2 class.
 *
 * author: Stefan Buettcher
 * created: 2007-07-14
 * changed: 2007-07-16
 **/


#ifndef __INDEX__INDEX_ITERATOR2_H
#define __INDEX__INDEX_ITERATOR2_H


#include "index_iterator.h"


class IndexIterator2 : public IndexIterator {

	friend class CompactIndex;
	friend class HybridLexicon;
	friend class MultipleIndexIterator;

private:

	/**
	 * How many list segments have we seen for the current term? We need
	 * to keep track of this information so that we can decide when to jump
	 * across 64-bit markers and compressed synchronization points.
	 **/
	int segmentsSeen;

protected:

	/**
	 * Creates a new IndexIterator2 instance that reads data from "fileName"
	 * and uses a read buffer of size "bufferSize".
	 **/
	IndexIterator2(const char *fileName, int bufferSize);

public:

	/** Destructor. **/
	virtual ~IndexIterator2();

	virtual char *getClassName();

private:

	virtual void loadNextTerm();

}; // end of class IndexIterator2


#endif

