/**
 * author: Stefan Buettcher
 * created: 2007-11-18
 * changed: 2007-11-18
 **/


#include <stdlib.h>
#include "testing.h"
#include "../index/index_types.h"
#include "../misc/utils.h"


void TESTCASE_StartsWithEndsWith(int *passed, int *failed) {
	*passed = *failed = 0;

	EXPECT(startsWith("Test123", "Test"));
	EXPECT(!startsWith("Test", "Test1234"));
	EXPECT(startsWith("Test", "test", false));
	EXPECT(startsWith("SomeString", ""));

	EXPECT(endsWith("bdlkshad", "shad"));
	EXPECT(!endsWith("SomeString", "string"));
	EXPECT(!endsWith("string", "somestring"));
	EXPECT(endsWith("bdlkshad", 6, "lksh", 4));
	EXPECT(!endsWith("bdlkshad", 6, "shad", 4));
	EXPECT(endsWith("SomeString", "estring", false));
	EXPECT(endsWith("SomeString", 6, "mest", 4, false));
	EXPECT(endsWith("blah", ""));
} // end of TESTCASE_StartsWithEndsWith(int*, int*)



