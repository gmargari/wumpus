/**
 * author: Stefan Buettcher
 * created: 2007-09-06
 * changed: 2009-02-01
 **/


#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <vector>
#include "testing.h"


using namespace std;


static vector<string> *testCaseList = NULL;
static map<string,TestCaseFunction> *testCaseMap = NULL;


bool registerTestCase(const char *name, TestCaseFunction function) {
	if (testCaseList == NULL) {
		testCaseList = new vector<string>();
		testCaseMap = new map<string,TestCaseFunction>();
	}
	if (testCaseMap->find(name) != testCaseMap->end()) {
		if ((*testCaseMap)[name] == function)
			return false;
		fprintf(stderr, "(ERROR) Duplicate test case: %s.\n", name);
		exit(1);
	}
	testCaseList->push_back(name);
	(*testCaseMap)[name] = function;
	return true;
} // end of registerTestCase(char*, TestCaseFunction)


void runAllTestCases() {
	int passed = 0, failed = 0;
	for (int i = 0; i < testCaseList->size(); i++) {
		printf("------------------------------------------------------------\n");
		printf("Running test: %s\n", (*testCaseList)[i].c_str());
		TestCaseFunction function = (*testCaseMap)[(*testCaseList)[i]];
		int p, f;
		function(&p, &f);
		printf("  %d/%d test cases passed.\n", p, p + f);
		passed += p;
		failed += f;
	}
	printf("------------------------------------------------------------\n");
	printf("Total: %d/%d test cases passed.\n", passed, passed + failed);
} // end of runAllTestCases()


void runTestCase(const char *name) {
	if (testCaseMap->find(name) == testCaseMap->end()) {
		fprintf(stderr, "(ERROR) Unable to locate test case: \"%s\".\n", name);
		exit(1);
	}
	printf("Running test: %s\n", name);
	TestCaseFunction function = (*testCaseMap)[name];
	int p, f;
	function(&p, &f);
	printf("  %d/%d test cases passed.\n", p, p + f);
} // end of runTestCase(char*)


