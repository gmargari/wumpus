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
 * Definition of the class ExtentList. Instances of ExtentList can hold lists of
 * extents (i.e. start-end tuples that define a range within the indexed text).
 * The extents managed by this class and its extensions adhere to the rules for
 * generalized concordance lists introduced by Clarke and Burkowski.
 *
 * You can find the documentation for the functions in
 *   C. Clarke: "An Algebra for Structured Text Search..."
 *
 * author: Stefan Buettcher
 * created: 2004-09-02
 * changed: 2007-09-07
 **/


#ifndef __INDEX__EXTENTLIST_H
#define __INDEX__EXTENTLIST_H


#include "../index/index_types.h"
#include "../misc/lockable.h"
#include <stdio.h>


class ExtentList;
class VisibleExtents;


/** An ExtentList is sort of an iterator over all the extents managed by the object. **/
class ExtentList : public Lockable {

	friend class ExtentList_Copy;
	friend class Optimizer;
	friend class Simplifier;

public:

	static const int TYPE_EXTENTLIST = 0;
	static const int TYPE_POSTINGLIST = 1;
	static const int TYPE_SEGMENTEDPOSTINGLIST = 2;
	static const int TYPE_EXTENTLIST_OR = 3;
	static const int TYPE_EXTENTLIST_AND = 4;
	static const int TYPE_EXTENTLIST_CONTAINMENT = 5;
	static const int TYPE_EXTENTLIST_EMPTY = 6;
	static const int TYPE_EXTENTLIST_ORDERED = 7;
	static const int TYPE_EXTENTLIST_FROMTO = 8;
	static const int TYPE_EXTENTLIST_RANGE = 9;
	static const int TYPE_EXTENTLIST_SEQUENCE = 10;
	static const int TYPE_EXTENTLIST_BIGRAM = 11;

	static const int TYPE_EXTENTLIST_SECURITY = 20;
	static const int TYPE_EXTENTLIST_CACHED = 21;

	static const int TAKE_OWNERSHIP = 1;
	static const int DO_NOT_TAKE_OWNERSHIP = 2;

protected:

	/**
	 * Total length of the list. Initialized to -1, later used to cache the
	 * result of a call to getLength().
	 **/
	offset length;

	/** Same as length, but for use with @count[size] queries. **/
	offset totalSize;

	/** Indicates whether the optimize() method has already been executed. **/
	bool alreadyOptimized;

	/** Can be either TAKE_OWNERSHIP (default) or DO_NOT_TAKE_OWNERSHIP. **/
	int ownershipOfChildren;

public:

	ExtentList();

	virtual ~ExtentList();

	/** Implementation of Clarke's Tau function. **/
	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);

	/** Implementation of Clarke's Rho function. **/
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);

	/** Implementation of Clarke's Rho' function. **/
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);

	/** Implementation of Clarke's Tau' function. **/
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	/**
	 * Returns the next "n" extents, starting at "from". Data are stored
	 * at memory referenced by "start" and "end". Returns the number of extents
	 * retrieved, something between 0 and "n".
	 **/
	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	/**
	 * Returns the number of postings between the offsets "start" and "end", i.e.
	 * the cardinality of the set of extents that start at or after "start" and
	 * end at or before "end".
	 **/
	virtual offset getCount(offset start, offset end);

	/** Returns the same as "getCount(0, MAX_OFFSET)". **/
	virtual offset getLength();

	/** Returns the sum of the sizes of all elements in this list. **/
	virtual offset getTotalSize();

	/** Returns the n-th extent in this list, starting from position 1. **/
	virtual bool getNth(offset n, offset *start, offset *end);

	/** Returns the type of the list, e.g. TYPE_EXTENTLIST. **/
	virtual int getType();

	/**
	 * Runs some optimizations. Nested ANDs and nested ORs are combined.
	 * Sequences are evaluated immediately, etc.
	 **/
	virtual void optimize();

	/**
	 * Returns true iff the ExtentList instance is guaranteed to be consistent
	 * with the security model. This information is used by the query processor
	 * to add new restrictions whenever necessary.
	 * By default, every ExtentList instance is non-secure. Override the method
	 * if you are secure.
	 **/
	virtual bool isSecure();

	/**
	 * Returns the memory consumption of this ExtentList instance (including
	 * sublists, such as in ExtentList_OR, Extentlist_Sequence, ...) in bytes.
	 **/
	virtual long getMemoryConsumption();

	/**
	 * Puts the desired value into the given target buffer. Returns true if
	 * the value exists and the buffer is big enough. False otherwise.
	 *
	 * Example:
	 *   double avgSize;
	 *   list->getInternalValue("AVG_SIZE", &avgSize, sizeof(double));
	 **/
	virtual bool getInternalValue(char *key, void *target, int targetSize);

#if 0
	/**
	 * Returns the amount of memory that has to be at least occupied by this
	 * ExtentList and all its children.
	 **/
	virtual long getMinimalMemoryConsumption();

	/**
	 * Returns the number of lists in this operator tree that can adjust their
	 * memory consumption. This will be mainly SegmentedPostingList instances.
	 **/
	virtual long getListsWithVariableMemoryConsumption();

	/**
	 * Sets the amount of memory available to every child of this list that
	 * supports variable memory consumption (mainly SegmentedPostingList instances).
	 **/
	virtual void setMemoryLimit(long limit);
#endif

	/**
	 * Returns a list that is consistent with the user's view of the file system,
	 * as defined by the content of "restriction".
	 **/
	virtual ExtentList *makeSecure(VisibleExtents *restriction);

	/**
	 * Returns true if either the thing is secure (isSecure() returns true) or
	 * it can be made secure by applying the restriction operator *once*. For
	 * example, applications of the OR operator are always almost secure, whereas
	 * applications of the "<" operator are not (because R(a < B) is not equiv.
	 * to R(R(a) < R(b)).
	 **/
	virtual bool isAlmostSecure();

	/**
	 * Returns a list that is a secure version of the instance on which the
	 * method is called. This can be either the original list or a modified version
	 * with one or more applications of the restriction operator. Please be aware
	 * that when you delete the list returned, the original list will be deleted
	 * as well, as the new list becomes the owner of the old list.
	 **/
	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	/**
	 * Returns a textual representation of the query that generated this list.
	 * Memory has to be freed by the caller.
	 **/
	virtual char *toString();

	/**
	 * Returns the internal position of the last extent returned or -1 if not
	 * supported.
	 **/
	virtual int getInternalPosition();

	virtual void detachSubLists();

	/**
	 * Takes a bunch of document-level posting lists and merges them into one big
	 * list. Frees the memory occupied by "lists" and the ExtentList instances
	 * contained in that list.
	 **/
	static ExtentList *mergeDocumentLevelLists(ExtentList **lists, int listCount);

	/** Same as above, but for only two lists. **/
	static ExtentList *mergeDocumentLevelLists(ExtentList *list1, ExtentList *list2);

	/** Same as above, but uses RadixSort instead of a multiway merge operation. **/
	static ExtentList *radixMergeDocumentLevelLists(ExtentList **lists, int listCount);

}; // end of class ExtentList


/**
 * This class realizes efficient support for bigram queries like "United States".
 * Bigrams can also be used as parts of longer phrase queries.
 **/
class ExtentList_Bigram : public ExtentList {

protected:

	/**
	 * Pointer to the real posting list that contains the occurrences of the
	 * bigram we are interested in.
	 **/
	ExtentList *realList;

public:

	/**
	 * Creates a new ExtentList_Bigram instance. The new instance will take control
	 * of the given posting list (must be of type PostingList or SegmentedPostingList)
	 * and will free the list in the destructor.
	 **/
	ExtentList_Bigram(ExtentList *postingList) {
		realList = postingList;
	}

	~ExtentList_Bigram() {
		delete realList;
	}

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end) {
		if (!realList->getFirstStartBiggerEq(position, start, end))
			return false;
		*end = *end + 1;
		return true;
	}

	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end) {
		if (!realList->getFirstEndBiggerEq(position - 1, start, end))
			return false;
		*end = *end + 1;
		return true;
	}

	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end) {
		if (!realList->getLastStartSmallerEq(position, start, end))
			return false;
		*end = *end + 1;
		return true;
	}

	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end) {
		if (!realList->getLastEndSmallerEq(position - 1, start, end))
			return false;
		*end = *end + 1;
		return true;
	}

	virtual offset getLength() {
		return realList->getLength();
	}

	virtual offset getCount(offset start, offset end) {
		return realList->getCount(start, end - 1);
	}

	virtual long getMemoryConsumption() {
		return realList->getMemoryConsumption();
	}

	virtual void optimize() {
		realList->optimize();
	}

	virtual bool isSecure() {
		return false;
	}

	virtual bool isAlmostSecure() {
		return true;
	}

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction) {
		return this;
	}

	virtual char *toString() {
		return realList->toString();
	}

	virtual int getType() {
		return TYPE_EXTENTLIST_BIGRAM;
	}

}; // end of class ExtentList_Bigram


/** This class is used for things like "United States of America". **/
class ExtentList_Sequence : public ExtentList {

	friend class Optimizer;
	friend class Simplifier;

protected:

	/** Sublists. **/
	ExtentList **elem;

	/** Number of sublists (tokens in the sequence). **/
	int elemCount;

	/** Per-sublist information used in the getFirstBiggerEq etc. methods. **/
	offset *curStart, *curEnd;

	/**
	 * Merge input lists into one big ExtentList_Cached object if the memory
	 * requirement for this operation is less than COMPUTE_IMMEDIATE_THRESHOLD.
	 **/
	static const int COMPUTE_IMMEDIATE_THRESHOLD = 4 * 1024 * 1024;

	/**
	 * Number of tokens in the sequence. This can be different from elemCount if
	 * some of the element lists represent bigrams instead of individual terms.
	 **/
	int tokenLength;

public:

	ExtentList_Sequence(ExtentList **elements, int count);

	~ExtentList_Sequence();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	virtual long getMemoryConsumption();

	virtual void optimize();

	/** Returns true if all elements are secure. **/
	virtual bool isSecure();

	/** Returns true if all elements are at least almost secure. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

	virtual int getType();

}; // end of class ExtentList_Sequence


/** This class is used for the AND operator. **/
class ExtentList_AND : public ExtentList {

	friend class Optimizer;
	friend class Simplifier;

protected:

	ExtentList **elem;

	int elemCount;

public:

	ExtentList_AND(ExtentList *operand1, ExtentList *operand2, int ownership = TAKE_OWNERSHIP);

	ExtentList_AND(ExtentList **elements, int count, int ownership = TAKE_OWNERSHIP);

	~ExtentList_AND();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual long getMemoryConsumption();

	virtual void optimize();

	virtual void detachSubLists();

	/** Returns true iff (elemCount == 1) and (elem[0]->isSecure()). **/
	virtual bool isSecure();

	/** Returns true if all elements are at least almost secure. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

private:

	void checkForMerge();

}; // end of class ExtentList_AND


/** This class is used for the OR operator. **/
class ExtentList_OR : public ExtentList {

	friend class CompactIndex;
	friend class Lexicon;
	friend class CompressedLexicon;
	friend class Optimizer;
	friend class ReallocLexicon;
	friend class Simplifier;
	friend class UncompressedLexicon;
	friend class TwoPassLexicon;

protected:

	ExtentList **elem;

	int elemCount;

	/**
	 * Merge input lists into one big ExtentList_Cached object if the memory
	 * requirement for this operation is less than MERGE_LISTS_THRESHOLD.
	 **/
	static const int MERGE_LISTS_THRESHOLD = 4 * 1024 * 1024;

public:

	ExtentList_OR();

	ExtentList_OR(ExtentList *operand1, ExtentList *operand2, int ownership = TAKE_OWNERSHIP);

	ExtentList_OR(ExtentList **elements, int count, int ownership = TAKE_OWNERSHIP);

	~ExtentList_OR();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual long getMemoryConsumption();

	virtual int getType();

	virtual void optimize();

	virtual void detachSubLists();

	virtual void mergeChildLists();

	/** Returns true iff all elements are secure. **/
	virtual bool isSecure();

	/** Returns true iff all elements are at least almost secure. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

private:

	void checkForMerge();

}; // end of class ExtentList_OR



static const int ExtentList_OR_Postings__PREVIEW_SIZE = 64;

struct ExtentList_OR_Postings__PreviewStruct {
	offset currentValue;
	offset *preview;
	offset *current;
	offset *end;
	ExtentList *dataSource;
};


/** Special case of ExtentList_OR, to be used if all sub-lists are posting lists. **/
class ExtentList_OR_Postings : public ExtentList_OR {

private:

	ExtentList_OR_Postings__PreviewStruct *previewArray;
	ExtentList_OR_Postings__PreviewStruct **heap;

public:

	ExtentList_OR_Postings(ExtentList *operand1, ExtentList *operand2);

	ExtentList_OR_Postings(ExtentList **elements, int count);

	~ExtentList_OR_Postings();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getCount(offset start, offset end);

	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	virtual void optimize();

}; // end of class ExtentList_OR_Postings


/** This class is used for the "containing" and "contained in" operators. **/
class ExtentList_Containment : public ExtentList {

	friend class Optimizer;
	friend class Simplifier;
	friend class RankedQuery;

protected:

	ExtentList *container;

	ExtentList *containee;

	/** Tells us whether the operation is inverted ("not containing"). **/
	bool inverted;

	/** Do we have to return container or containee extents? **/
	bool returnContainer;

	/** For speeding up certain types of queries. **/
	offset lastContainerStart, lastContainerEnd;

public:

	ExtentList_Containment(ExtentList *container, ExtentList *containee,
	                       bool returnContainer, bool inverted);

	~ExtentList_Containment();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual int getNextN(offset from, offset to, int n, offset *start, offset *end);

	virtual long getMemoryConsumption();

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	virtual char *toString();

	virtual void optimize();

	virtual int getType();

	virtual void detachSubLists();

	/**
	 * Returns false if we are in "return containee" mode. Otherwise, returns
	 * true iff the container is secure.
	 **/
	virtual bool isSecure();

	/**
	 * Returns false if we are in "return containee" mode. Otherwise, returns
	 * true iff the container is at least almost secure.
	 **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

}; // end of class ExtentList_Containment


/** This class is used for the ".." constructions. **/
class ExtentList_FromTo : public ExtentList {

	friend class GCLQuery;
	friend class BM25Query;

protected:

	ExtentList *from;

	ExtentList *to;

public:

	ExtentList_FromTo(ExtentList *from, ExtentList *to);

	~ExtentList_FromTo();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual long getMemoryConsumption();

	virtual offset getLength();

	virtual offset getTotalSize();

	virtual void optimize();

	/** ExtentList_FromTo is never secure. **/
	virtual bool isSecure();

	/** Returns true if both "from" and "to" are at least almost secure. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual int getType();

	virtual char *toString();

	virtual void detachSubLists();

}; // end of class ExtentList_FromTo


/** This class is used for things like [23]<[42]. **/
class ExtentList_Range : public ExtentList {

	friend class Simplifier;

protected:

	offset width;

	offset maxOffset;

public:

	ExtentList_Range(offset width, offset maxOffset);

	~ExtentList_Range();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	/** Returns false. **/
	virtual bool isSecure();

	/** Returns true. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

	virtual int getType();

}; // end of class ExtentList_Range


/** This class is used for things like 23. **/
class ExtentList_OneElement : public ExtentList {

private:

	offset from;

	offset to;

public:

	ExtentList_OneElement(offset from, offset to);

	~ExtentList_OneElement();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getLength();

	/** Returns false. **/
	virtual bool isSecure();

	/** Returns true. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

}; // end of class ExtentList_OneElement


/** This class is used for empty lists. **/
class ExtentList_Empty : public ExtentList {

public:

	ExtentList_Empty();

	~ExtentList_Empty();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getLength();

	virtual offset getCount(offset start, offset end);

	/** The empty list is always secure. **/
	virtual bool isSecure();

	/** The empty list is alway almost secure. **/
	virtual bool isAlmostSecure();

	virtual int getType();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

}; // end of class ExtentList_Empty


class ExtentList_Copy : public ExtentList {

private:

	ExtentList *original;

public:

	/**
	 * Creates a new list that is a copy of the new list. Does not take control of
	 * the memory allocated by the original list.
	 **/
	ExtentList_Copy(ExtentList *orig);

	~ExtentList_Copy();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getLength();
	virtual offset getCount(offset start, offset end);

	/** The copy is secure iff the original ExtentList object is secure. **/
	virtual bool isSecure();

	/** The same applies to "almost secure". **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

}; // end of class ExtentList_Copy


/**
 * The ExtentList_OrderedCombination class takes a list of ExtentList instances whose
 * members have to be strictly ordered, i.e. the postings in the i-th list have to come
 * before the postings in the (i+1)-th list. The result is equivalent to ExtentList_OR
 * but much faster.
 **/
class ExtentList_OrderedCombination : public ExtentList {

	friend class Optimizer;
	friend class Simplifier;

public:

	/** Underlying ExtentList instances. **/
	ExtentList **lists;

	/** Number of underlying lists. **/
	int listCount;

	/**
	 * In order to determine quickly which one of the underlying lists can serve
	 * a certain request, we need to know where the individual lists start.
	 **/
	offset *firstStart, *lastStart, *firstEnd, *lastEnd;

	/** We can do a shift operation on the content of the sub-lists. **/
	offset *relativeOffsets;

private:

	/**
	 * This guy tells us which sub-index has last been accessed. We hope that
	 * this will give us a little performance improvement.
	 **/
	int currentSubIndex;

public:

	ExtentList_OrderedCombination(ExtentList **lists, int listCount);

	ExtentList_OrderedCombination(ExtentList **lists, offset *relOffs, int listCount);

	~ExtentList_OrderedCombination();

	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	virtual offset getLength();	

	virtual offset getCount(offset start, offset end);

	virtual long getMemoryConsumption();
	
	virtual void optimize();

	/** Returns true iff all elements are secure. **/
	virtual bool isSecure();

	/** Returns true iff all elements are at least almost secure. **/
	virtual bool isAlmostSecure();

	virtual ExtentList *makeAlmostSecure(VisibleExtents *restriction);

	virtual char *toString();

	virtual int getType();

private:

	void initialize(ExtentList **lists, offset *relOffs, int count);

}; // end of class ExtentList_OrderedCombination


#endif



