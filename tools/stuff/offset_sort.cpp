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
 * The purpose of this program is to test the relative performance of
 * MergeSort and RadixSort on a list of random offset values.
 *
 * author: Stefan Buettcher
 * created: 2005-05-11
 * changed: 2005-05-11
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>


#define offset int32_t

typedef unsigned char byte;

int n;

offset *array1;
offset *array2;
offset *array3;


int currentTimeMillis() {
	struct timeval currentTime;
	int result = gettimeofday(&currentTime, NULL);
	int seconds = currentTime.tv_sec;
	int microseconds = currentTime.tv_usec;
	return (seconds * 1000) + (microseconds / 1000);
} // end of currentTimeMillis()


void createRandomSequence() {
	for (int i = 0; i < n; i++)
		array1[i] = random() % 2000000000;
} // end of createRandomSequence()


static void mergeSort(offset *array, int n, offset *tempArray) {
	if (n <= 7) {
		for (int j = 0; j < n; j++) {
			int best = j;
			for (int k = j + 1; k < n; k++)
				if (array[k] < array[best])
					best = k;
			offset temp = array[best];
			array[best] = array[j];
			array[j] = temp;
		}
	}
	else {
		int middle = (n >> 1);
		mergeSort(array, middle, tempArray);
		mergeSort(&array[middle], n - middle, tempArray);
		int leftPos = 0;
		int rightPos = middle;
		int outPos = 0;
		while (true) {
			if (array[leftPos] <= array[rightPos]) {
				tempArray[outPos++] = array[leftPos];
				if (++leftPos >= middle)
					break;
			}
			else {
				tempArray[outPos++] = array[rightPos];
				if (++rightPos >= n)
					break;
			}
		}
		while (leftPos < middle)
			tempArray[outPos++] = array[leftPos++];
		while (rightPos < n)
			tempArray[outPos++] = array[rightPos++];
		memcpy(array, tempArray, n * sizeof(offset));
	}
} // end of mergeSort(offset*, int, offset*)


void sortByMergeSort() {
	memcpy(array2, array1, n * sizeof(offset));
	int now = currentTimeMillis();
	mergeSort(array2, n, array3);
	int then = currentTimeMillis();
	printf("Time elapsed for MergeSort:  %d\n", (then - now));
} // end of sortByMergeSort()


void radixSort(offset *inArray, int n, int shift, offset *outArray) {
	int count[256];
	int pos[256];
	memset(count, 0, sizeof(count));
	for (int i = 0; i < n; i++) {
		int bucket = ((inArray[i] >> shift) & 255);
		count[bucket]++;
	}
	pos[0] = 0;
	for (int i = 1; i < 256; i++)
		pos[i] = pos[i - 1] + count[i - 1];
	for (int i = 0; i < n; i++) {
		int bucket = ((inArray[i] >> shift) & 255);
		outArray[pos[bucket]] = inArray[i];
		pos[bucket]++;
	}
} // end of radixSort(offset*, int, int, offset*)


void sortByRadixSort() {
	memcpy(array2, array1, n * sizeof(offset));
	int now = currentTimeMillis();
	for (int i = 0; i < sizeof(offset); i++) {
		if ((i & 1) == 0)
			radixSort(array2, n, i * 8, array3);
		else
			radixSort(array3, n, i * 8, array2);
	}
	int then = currentTimeMillis();
	printf("Time elapsed for RadixSort:  %d\n", (then - now));
} // end of sortByRadixSort()


int main() {
	printf("Number of integers to sort: ");
	scanf("%d", &n);
	printf("\n");
	array1 = new offset[n];
	array2 = new offset[n];
	array3 = new offset[n];
	createRandomSequence();
	sortByMergeSort();
	for (int i = 1; i < n; i++)
		assert(array2[i] >= array2[i - 1]);
	sortByRadixSort();
	for (int i = 1; i < n; i++)
		assert(array2[i] >= array2[i - 1]);
	return 0;
} // end of main()


