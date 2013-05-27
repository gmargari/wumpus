/**
 * Definition of the ExtentList_Transformation class. ExtentList_Transformation
 * realizes an address space transformation on a given ExtentList instance. If
 * the ExtentList instance is a PostingList or a SegmentedPostingList of limited
 * size, the whole list is transformed immediately, resulting in optimal query
 * processing performance for the access methods later on. In all other cases
 * (real ExtentList, not just postings, or list too long), the transformation
 * is realized on-the-fly, whenever one of the access methods is called.
 *
 * author: Stefan Buettcher
 * created: 2006-01-26
 * changed: 2007-04-01
 **/


#ifndef __EXTENTLIST__EXTENTLIST_TRANSFORMATION_H
#define __EXTENTLIST__EXTENTLIST_TRANSFORMATION_H


#include "extentlist.h"
#include "address_space_transformation.h"


class ExtentList_Transformation : public ExtentList {

public:

	/**
	 * This is the limit for immediate in-memory transformation. If the source
	 * list is short than this many postings, the transformation takes place
	 * immediately, in memory. Otherwise, the transformation is performed on
	 * demand, within each access method.
	 **/
	static const int TRANSFORM_IN_MEMORY_LIMIT = 50000000;

private:

	/** The underlying list that we are transforming. **/
	ExtentList *list;

	/** The rule set that defines the transformation. **/
	AddressSpaceTransformation *transformation;

public:

	ExtentList_Transformation(ExtentList *l, AddressSpaceTransformation *t);

	virtual ~ExtentList_Transformation();

	/**
	 * Returns a new ExtentList instance, corresponding to the transformed
	 * version of the given list under the given transformation. The resulting
	 * list will be either an instance of the class PostingList or of the class
	 * ExtentList_Transformation, depending on how long the input list is
	 * (threshold TRANSFORM_IN_MEMORY_LIMIT).
	 * The method will claim ownership of both "list" and "transformation" and
	 * will delete them when appropriate.
	 **/
	static ExtentList *transformList(ExtentList *list, AddressSpaceTransformation *transformation);

	/** Implementation of Clarke's Tau function. **/
	virtual bool getFirstStartBiggerEq(offset position, offset *start, offset *end);

	/** Implementation of Clarke's Rho function. **/
	virtual bool getFirstEndBiggerEq(offset position, offset *start, offset *end);

	/** Implementation of Clarke's Rho' function. **/
	virtual bool getLastStartSmallerEq(offset position, offset *start, offset *end);

	/** Implementation of Clarke's Tau' function. **/
	virtual bool getLastEndSmallerEq(offset position, offset *start, offset *end);

	/** Returns the same as "getCount(0, MAX_OFFSET)". **/
	virtual offset getLength();

	/** Returns the sum of the sizes of all elements in this list. **/
	virtual offset getTotalSize();

	virtual bool isSecure();

	virtual bool isAlmostSecure();

}; // end of class ExtentList_Transformation


#endif



