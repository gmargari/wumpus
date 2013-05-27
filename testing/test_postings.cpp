/**
 * author: Stefan Buettcher
 * created: 2007-09-06
 * changed: 2007-09-06
 **/


#include <stdlib.h>
#include "testing.h"
#include "../index/index_types.h"
#include "../misc/all.h"


static int cmp(const void *a, const void *b) {
	offset *x = (offset*)a;
	offset *y = (offset*)b;
	if (*x < *y)
		return -1;
	else if (*x > *y)
		return +1;
	else
		return 0;
} // end of cmp(void*, void*)


void TESTCASE_SortPostings(int *passed, int *failed) {
	static const int MAX_ARRAY_SIZE = 1000000;
	*passed = 0;
	*failed = 0;
	for (int i = 1; i < MAX_ARRAY_SIZE; i += (random() % i) + 1) {
		offset *array = typed_malloc(offset, i);
		offset *sorted = typed_malloc(offset, i);
		for (int k = 0; k < i; k++) {
			array[k] = random() % 1000000000;
			if (sizeof(offset) > 4)
				array[k] = array[k] * 1000000000 + random() % 1000000000;
			sorted[k] = array[k];
		}
		qsort(sorted, i, sizeof(offset), cmp);
		sortOffsetsAscending(array, i);
		for (int k = 0; k < i; k++)
			if (array[k] != sorted[k]) {
				fprintf(stderr, "  Test failed for array of size %d.\n", i);
				*failed = 1;
				return;
			}
		free(array);
		free(sorted);
	}
	*passed = 1;
} // end of TESTCASE_SortPostings(int*, int*)


void TESTCASE_SortPostingsReverse(int *passed, int *failed) {
	static const int MAX_ARRAY_SIZE = 1000000;
	static const int ITERATIONS_PER_SIZE = 10;
	*passed = 0;
	*failed = 0;
	for (int i = 1; i < MAX_ARRAY_SIZE; i += (random() % i) + 1) {
		offset *array = typed_malloc(offset, i);
		offset *sorted = typed_malloc(offset, i);
		for (int k = 0; k < i; k++) {
			array[k] = random() % 1000000000;
			if (sizeof(offset) > 4)
				array[k] = array[k] * 1000000000 + random() % 1000000000;
			sorted[k] = array[k];
		}
		qsort(sorted, i, sizeof(offset), cmp);
		sortOffsetsDescending(array, i);
		for (int k = 0; k < i; k++)
			if (array[k] != sorted[i - 1 - k]) {
				fprintf(stderr, "  Test failed for array of size %d.\n", i);
				*failed = 1;
				return;
			}
		free(array);
		free(sorted);
	}
	*passed = 1;
} // end of TESTCASE_SortPostingsReverse(int*, int*)



void TESTCASE_SortPostingsAndRemoveDuplicates(int *passed, int *failed) {
	static const int MAX_ARRAY_SIZE = 100000;
	static const int ITERATIONS_PER_SIZE = 10;
	*passed = 0;
	*failed = 0;
	for (int i = 100; i < MAX_ARRAY_SIZE; i += (random() % i) + 1) {
		offset *array = typed_malloc(offset, i);
		offset *sorted = typed_malloc(offset, i);
		for (int k = 0; k < i; k++) {
			array[k] = random() % (i + 100);
			sorted[k] = array[k];
		}
		qsort(sorted, i, sizeof(offset), cmp);
		int outPos = 1;
		for (int k = 1; k < i; k++)
			if (sorted[k] != sorted[k - 1])
				sorted[outPos++] = sorted[k];
		int unique = sortOffsetsAscendingAndRemoveDuplicates(array, i);
		if (unique != outPos) {
			fprintf(stderr, "  Test failed for array of size %d (incorrect number of uniques).\n", i);
			*failed = 1;
			return;
		}
		for (int k = 0; k < unique; k++)
			if (array[k] != sorted[k]) {
				fprintf(stderr, "  Test failed for array of size %d.\n", i);
				*failed = 1;
				return;
			}
		free(array);
		free(sorted);
	}
	*passed = 1;
} // end of TESTCASE_SortPostingsAndRemoveDuplicates(int*, int*)


