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
 * created: 2005-11-22
 * changed: 2006-05-10
 **/


#ifndef __INDEX__INPLACE_INDEX_ITERATOR_H
#define __INDEX__INPLACE_INDEX_ITERATOR_H


#include "compactindex.h"
#include "index_iterator.h"
#include "inplace_index.h"


class InPlaceIndexIterator : public IndexIterator {

protected:

	InPlaceIndex *index;

	/** Handle to the input file. **/
	int fileHandle;

	/** Buffer used to speed up index processing. **/
	byte *readBuffer;

	/** Current position inside the read buffer. **/
	int bufferPos;

	/** Number of bytes inside the read buffer. **/
	int bufferSize;

	/** Maximum number of bytes to read into the buffer. **/
	int maxBufferSize;

	/** Number of lists inside the CompactIndex instance. **/
	int listCount;

	/** Position inside the list of lists (number of lists done). **/
	int listPos;

	/** List of all terms in the index. **/
	InPlaceTermDescriptor **terms;

	/** Number of different terms in the index. **/
	int termCount;

	/** Number of the current term. **/
	int currentTerm;

	/** Position in the term's compressed list of segment descriptors. **/
	byte *posInCurrentTermsSegmentList;

public:

	/**
	 * Creates a new CompactIndexIterator instance that reads data from "fileName"
	 * and uses a read buffer of size "bufferSize".
	 **/
	InPlaceIndexIterator(InPlaceIndex *index, int bufferSize);

	/** Destructor. **/
	virtual ~InPlaceIndexIterator();

	/** Returns the number of terms in the index. **/
	virtual int64_t getTermCount();

	/** Returns the total number of list segments inside the index. **/
	virtual int64_t getListCount();

	/** Returns true iff there are more data to be returned. **/
	virtual bool hasNext();

	/**
	 * Returns a pointer to the next term inside the CompactIndex. No not touch
	 * the pointer or the data referenced, man! Do not free it! Returns NULL if
	 * there is no more term (end of index reached).
	 **/
	virtual char *getNextTerm();

	/**
	 * Returns the header of the next list or NULL if the end of the index has been
	 * reached. Do not touch it, do not free it.
	 **/
	virtual PostingListSegmentHeader *getNextListHeader();

	/**
	 * Returns the compressed posting list for the current position in the index.
	 * The length of the list (number of postings) is written to the memory
	 * referenced by "length", the size (number of bytes) to "size". If "buffer"
	 * is non-NULL, then the given buffer is used to store the postings.
	 **/
	virtual byte *getNextListCompressed(int *length, int *size, byte *buffer);

	/**
	 * Returns the uncompressed posting list for the current position in the index.
	 * The length of the list (number of postings) is written to the memory
	 * referenced by "length". If a buffer is given (non-NULL), then that buffer
	 * is used to store the postings.
	 **/
	virtual offset *getNextListUncompressed(int *length, offset *buffer);

	virtual void skipNext();

	/** Returns "InPlaceIndexIterator". Memory has to be freed by the caller. **/
	virtual char *getClassName();

private:

	/** Reads new data into the input buffer. **/
	void refillBufferIfNecessary();

}; // end of class InPlaceIndexIterator


#endif


