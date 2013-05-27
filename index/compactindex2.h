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
 * Definition of the CompactIndex2 class. CompactIndex2 is the new
 * implementation of the on-disk index structure. It uses a more compact
 * representation of term descriptors (most things are compressed using
 * front coding or differential coding) as well as more light-weight
 * in-memory data structures (front-coded and difference-coded as well).
 *
 * author: Stefan Buettcher
 * created: 2007-07-13
 * changed: 2007-09-08
 **/


#ifndef __INDEX__COMPACTINDEX2_H
#define __INDEX__COMPACTINDEX2_H


#include "compactindex.h"


static const int CI2_SIGNATURE_LENGTH = 22;
static const byte CI2_SIGNATURE[CI2_SIGNATURE_LENGTH] = {
	'W', 'u', 'm', 'p', 'u', 's', ':',
	'C', 'o', 'm', 'p', 'a', 'c', 't',
	'I', 'n', 'd', 'e', 'x', '2', 0, 26
};


struct CompactIndex2_Header {

	/** Number of distinct terms in index. **/
	int64_t termCount;

	/** Number of posting list segments. **/
	int64_t listCount;

	/** Number of postings. **/
	int64_t postingCount;

	/** Number of in-memory dictionary entries. **/
	int64_t descriptorCount;

	/** Byte-size of the compressed descriptor sequence. **/
	int64_t compressedDescriptorSize;

}; // end of struct CompactIndex2_Header


struct CompactIndex2_DictionaryGroup {

	/** First term in the given group. **/
	char groupLeader[MAX_TOKEN_LENGTH + 1];

	/** Byte position of this group in the compressed descriptor sequence. **/
	int32_t groupStart;

	/** File position of the group leader's posting list. **/
	int64_t filePosition;

}; // end of struct CompactIndex2_DictionaryGroup


class CompactIndex2 : public CompactIndex {

	friend class CompactIndex;
	friend class TerabyteQuery;

private:

	CompactIndex2_Header header;

	/**
	 * Block leader in current (i.e., last) index block. We need this information
	 * to front-code the term strings of the block leaders.
	 **/
	char firstTermInLastBlock[MAX_TOKEN_LENGTH + 1];

	/** Compressed in-memory dictionary. **/
	byte *compressedDescriptors;

	/** Number of bytes allocated for, and used by, compressed descriptors. **/
	uint32_t allocatedForDescriptors, usedByDescriptors;

	/** Uncompressed group descriptors for the compressed dictionary. **/
	CompactIndex2_DictionaryGroup *groupDescriptors;

	/** Number of dictionary groups. **/
	int dictionaryGroupCount;

	/** Byte position of the last byte of postings data in the index. **/
	int64_t endOfPostingsData;

	/** Last posting for current term. **/
	offset currentTermLastPosting;

	/** Number of pending segment headers for current term. **/
	int32_t currentTermSegmentCount;

	int64_t currentTermMarker;

	/** Pending segment header data for current term. **/
	byte *temporaryPLSH;
	int32_t allocatedForPLSH, usedByPLSH;

protected:

	/** Creates new index object for on-disk inverted file. **/
	CompactIndex2(Index *owner, const char *fileName, bool create, bool use_O_DIRECT);

	/** Creates new index object for in-memory inverted file. **/
	CompactIndex2(Index *owner, const char *fileName);

	virtual void initializeForQuerying();

public:

	virtual ~CompactIndex2();

	/**
	 * Returns true if the given file contains an inverted index in CompactIndex2
	 * format. Otherwise (or if the file does not exist), it returns false.
	 **/
	static bool canRead(const char *fileName);

	/** Same as above, but with a compressed postings list. **/
	virtual void addPostings(const char *term, byte *compressedPostings,
			int byteLength, int count, offset first, offset last);

	virtual void flushWriteCache();

	virtual void getClassName(char *target);

	virtual int64_t getTermCount();

	virtual int64_t getPostingCount();

protected:

	void copySegmentsToWriteCache();

	void addDescriptor(const char *term);

	void updateMarker();

	/**
	 * Internal function, used by getPostings(char*). getPostings2 is a straight-
	 * forward implementation that performs a binary search on the term list and
	 * returns an ExtentList instance containing all postings for the given term.
	 * getPostings implements all the fancy stuff, like wildcards and stemming.
	 **/
	virtual ExtentList *getPostings2(const char *term);

	/**
	 * Returns an ExtentList_OR instance that contains the postings for all
	 * terms matching the given wildcard query. If "stem" is non-NULL,
	 * only the postings for terms who stem to "stem" are returned.
	 **/
	virtual ExtentList *getPostingsForWildcardQuery(const char *pattern, const char *stem);

	/**
	 * Returns the file position of the on-disk index block containing the given
	 * term, or -1 if no such block exists.
	 **/
	int64_t getBlockStart(const char *term, char *groupLeader);

}; // end of class CompactIndex2


#endif


