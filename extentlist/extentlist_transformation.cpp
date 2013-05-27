/**
 * Implementation of the ExtentList_Transformation class.
 *
 * author: Stefan Buettcher
 * created: 2006-01-26
 * changed: 2007-04-01
 **/


#include "extentlist_transformation.h"
#include "simplifier.h"
#include "../index/index_compression.h"
#include "../index/index_types.h"
#include "../index/postinglist.h"
#include "../misc/all.h"


static const char *LOG_ID = "ExtentList_Transformation";


ExtentList * ExtentList_Transformation::transformList(
		ExtentList *list, AddressSpaceTransformation *transformation) {
	bool doNothing = (list == NULL);
	if (list != NULL) {
		list = Simplifier::simplifyList(list);
		if (list->getType() == TYPE_EXTENTLIST_EMPTY)
			doNothing = true;
	}
	if (doNothing) {
		delete transformation;
		return list;
	}

//	if ((list->getType() != TYPE_POSTINGLIST) && (list->getType() != TYPE_SEGMENTEDPOSTINGLIST)) {
//		char msg[256];
//		sprintf(msg, "Transforming list of type %d", list->getType());
//		log(LOG_ERROR, LOG_ID, msg);
//	}
//	assert((list->getType() == TYPE_POSTINGLIST) || (list->getType() == TYPE_SEGMENTEDPOSTINGLIST));

	if (list->getLength() > TRANSFORM_IN_MEMORY_LIMIT) {
		assert(false);
		return new ExtentList_Transformation(list, transformation);
	}
	else {
		int count = list->getLength();
		offset *postings = typed_malloc(offset, count + 1);
		int n = list->getNextN(0, MAX_OFFSET, count, postings, postings);
		assert(n == count);
		transformation->transformSequence(postings, count);
		delete list;
		delete transformation;
		return new PostingList(postings, count, false, true);
	}
} // end of transformList(ExtentList*, AddressSpaceTransformation*)


ExtentList_Transformation::ExtentList_Transformation(
		ExtentList *list, AddressSpaceTransformation *transformation) {
	this->list = list;
	this->transformation = transformation;
} // end of ExtentList_Transformation(...)


ExtentList_Transformation::~ExtentList_Transformation() {
	delete list;
	list = NULL;
	delete transformation;
	transformation = NULL;
} // end of ~ExtentList_Transformation()


bool ExtentList_Transformation::getFirstStartBiggerEq(offset position, offset *start, offset *end) {
	assert(false);
	return false;
} // end of getFirstStartBiggerEq(offset, offset*, offset*)


bool ExtentList_Transformation::getFirstEndBiggerEq(offset position, offset *start, offset *end) {
	assert(false);
	return false;
} // end of getFirstEndBiggerEq(offset, offset*, offset*)


bool ExtentList_Transformation::getLastStartSmallerEq(offset position, offset *start, offset *end) {
	assert(false);
	return false;
} // end of getLastStartSmallerEq(offset, offset*, offset*)


bool ExtentList_Transformation::getLastEndSmallerEq(offset position, offset *start, offset *end) {
	assert(false);
	return false;
} // end of getLastEndSmallerEq(offset, offset*, offset*)


bool ExtentList_Transformation::isSecure() {
	return false;
}


bool ExtentList_Transformation::isAlmostSecure() {
	return list->isAlmostSecure();
}


offset ExtentList_Transformation::getLength() {
	return list->getLength();
}


offset ExtentList_Transformation::getTotalSize() {
	return list->getTotalSize();
}


