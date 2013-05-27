/**
 * author: Stefan Buettcher
 * created: 2007-09-06
 * changed: 2007-09-06
 **/


#ifndef __TESTING__TESTING_H
#define __TESTING__TESTING_H


typedef void (*TestCaseFunction)(int *passed, int *failed);


bool registerTestCase(const char *name, TestCaseFunction function);

void runTestCase(const char *name);

void runAllTestCases();


#define REGISTER_TEST_CASE(NAME) \
	void TESTCASE_##NAME(int *passed, int *failed); \
	static bool testcasedummy##NAME = registerTestCase(#NAME, TESTCASE_##NAME);


#define EXPECT(expr) if (expr) ++*passed; else ++*failed;


#include "test_compression.h"
#include "test_postings.h"
#include "test_utils.h"


#endif


