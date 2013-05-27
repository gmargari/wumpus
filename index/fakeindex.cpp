/**
 * author: Stefan Buettcher
 * created: 2007-03-23
 * changed: 2007-03-23
 **/


#include <assert.h>
#include "fakeindex.h"
#include "../extentlist/extentlist.h"


ExtentList * FakeIndex::getPostings(const char *term, uid_t userID) {
	return new ExtentList_Empty();
}


ExtentList * FakeIndex::getPostings(const char *term, uid_t userID, bool fromDisk, bool fromMemory) {
	return new ExtentList_Empty();
}


void FakeIndex::getPostings(char **terms, int termCount, uid_t userID, ExtentList **results) {
	assert(results != NULL);
	for (int i = 0; i < termCount; i++)
		results[i] = new ExtentList_Empty();
}

			
