/**
 * This file contains the main method used to run a bunch of test cases.
 * Either run without parameters, or explicitly list the test case to run.
 * Executing the program with argument "--list" produces a list of all
 * test cases.
 *
 * author: Stefan Buettcher
 * created: 2007-09-06
 * changed: 2007-09-06
 **/


#include <stdio.h>
#include "../testing/testing.h"


int main(int argc, char **argv) {
	if (argc == 1)
		runAllTestCases();
	else if (argc > 2) {
		fprintf(stderr, "Usage:  run_testcases [--list|NAME_OF_TEST_TO_RUN]\n\n");
		return 1;
	}
	else {
		char *testCase = argv[1];
		runTestCase(testCase);
	}
	return 0;
} // end of main(int, char**)


