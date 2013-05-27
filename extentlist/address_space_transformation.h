/**
 * Defines a helper structure AddressSpaceTransformation that is used to realize
 * partial file changes, e.g. APPEND operations.
 *
 * author: Stefan Buettcher
 * created: 2006-01-26
 * changed: 2007-04-01
 **/


#ifndef __EXTENTLIST__ADDRESS_SPACE_TRANSFORMATION_H
#define __EXTENTLIST__ADDRESS_SPACE_TRANSFORMATION_H


#include "../index/index_types.h"
#include "../misc/all.h"


struct TransformationElement {

	/** Start address of source region. **/
	offset source;

	/** Where to map the source region? **/
	offset destination;

	/** Length of the mapping (number of postings affected). **/
	uint32_t length;

}; // end of struct TransformationElement


class AddressSpaceTransformation {

public:

	/** Number of rules in this transformation object. **/
	int count;

	/** List of source offsets. **/
	offset *source;

	/** List of target offsets. **/
	offset *destination;

	/** Number of postings affected by each rule. **/
	uint32_t *length;

public:

	/**
	 * Creates a new AddressSpaceTransformation object from the given data. Does
	 * not assume ownership of the data stored in "rules".
	 **/
	AddressSpaceTransformation(TransformationElement *rules, int count);

	virtual ~AddressSpaceTransformation();

	/**
	 * Returns a new AddressSpaceTransformation object that represents the inverse
	 * transformation of the one represented by this instance.
	 **/
	AddressSpaceTransformation *invert();

	/**
	 * Transforms the given posting sequence to a new one, according to the rules
	 * found in this transformation object.
	 **/
	void transformSequence(offset *postings, int count);

public:

	/**
	 * The following stuff is for append operations. Highly experimental, unstable,
	 * undocumented, dangerous. Don't use this.
	 **/

	static void setInitialTokenCount(offset fileStart, offset tokenCount);

	static offset getInitialTokenCount(offset fileStart);

	static void removeRules(offset fileStart);

	static void updateRules(offset oldFileStart, offset newFileStart, offset length);

	static AddressSpaceTransformation *getRules();
	
}; // end of class AddressSpaceTransformation


#endif



