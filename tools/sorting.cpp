#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>


#define ITER 10
#define T int
#ifndef MIN
	#define MIN(a, b) (a < b ? a : b)
#endif


static double getTime() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + 1E-6 * tv.tv_usec;
}

static int cmp(const void *a, const void *b) {
	T *x = (T*)a;
	T *y = (T*)b;
	if (*x < *y)
		return -1;
	else if (*x > *y)
		return +1;
	else
		return 0;
}

void selectionSort(T *array, int arraySize, T *out) {
	memcpy(out, array, arraySize * sizeof(T));
	for (int i = 0; i < arraySize; i++) {
		int best = i;
		for (int k = i + 1; k < arraySize; k++)
			if (out[k] < out[best])
				best = k;
		T tmp = out[i];
		out[i] = out[best];
		out[best] = tmp;
	}		
}

void heapSort(T *array, int arraySize, T *out) {
	// establish heap property, incrementally
	for (int i = 0; i < arraySize; i++) {
		int node = i, nodeValue = array[i];
		int parent = ((node - 1) >> 1);
		while ((node > 0) && (nodeValue > out[parent])) {
			out[node] = out[parent];
			node = parent;
			parent = ((node - 1) >> 1);
		}
		out[node] = nodeValue;
	}
	// repeatedly extract maximum element and put at end of array
	while (arraySize > 1) {
		T toInsert = out[--arraySize];
		out[arraySize] = out[0];
		int node = 0, child = 1;
		while (child < arraySize) {
			if (child + 1 < arraySize)
				if (out[child + 1] > out[child])
					child++;
			if (toInsert >= out[child])
				break;
			out[node] = out[child];
			node = child;
			child = node + node + 1;
		}
		out[node] = toInsert;
	}
}

void mergeSortRecursive(T *array, int arraySize, T *out, T *temp) {
	if (arraySize < 8)
		selectionSort(array, arraySize, out);
	else {
		int middle = arraySize / 2;
		mergeSortRecursive(array, middle, temp, out);
		mergeSortRecursive(&array[middle], arraySize - middle, &temp[middle], out);
		int leftPos = 0, rightPos = middle, outPos = 0;
		T l = temp[0], r = temp[middle];
		while (true) {
			if (l <= r) {
				out[outPos++] = l;
				l = temp[++leftPos];
				if (leftPos >= middle) {
					while (rightPos < arraySize)
						out[outPos++] = temp[rightPos++];
					break;
				}
			}
			else {
				out[outPos++] = r;
				r = temp[++rightPos];
				if (rightPos >= arraySize) {
					while (leftPos < middle)
						out[outPos++] = temp[leftPos++];
					break;
				}
			}
		}
	}
}

void mergeSortIterative(T *array, int cnt, T *temp) {
	for (int i = 0; i < cnt; i += 4) {
		selectionSort(&array[i], 4, temp);
		memcpy(&array[i], temp, 4 * sizeof(T));
	}
	for (int windowSize = 8; windowSize < 2 * cnt; windowSize <<= 1) {
		for (int start = 0; start < cnt; start += windowSize) {
			int leftPos = start, leftEnd = start + (windowSize >> 1);
			if (leftEnd >= cnt)
				break;
			int rightPos = leftEnd, rightEnd = MIN(start + windowSize, cnt);
			int outPos = 0;
			while (true) {
				if (array[leftPos] <= array[rightPos]) {
					temp[outPos++] = array[leftPos++];
					if (leftPos >= leftEnd) {
						while (rightPos < rightEnd)
							temp[outPos++] = array[rightPos++];
						break;
					}
				}
				else {
					temp[outPos++] = array[rightPos++];
					if (rightPos >= rightEnd) {
						while (leftPos < leftEnd)
							temp[outPos++] = array[leftPos++];
						break;
					}
				}
			}
			memcpy(&array[start], temp, (rightEnd - start) * sizeof(T));
		}
	}
}

void radixSort(T *array, int arraySize, T *temp) {
#if 1
	// collect statistics
	int cnt[4][256];
	memset(cnt, 0, sizeof(cnt));
	for (int k = 0; k < arraySize; k++) {
		T value = array[k];
		for (int i = 0; i < 4; i++) {
			cnt[i][value & 255]++;
			value >>= 8;
		}
	}
	// compute start positions of output chunks, from statistics gathered
	for (int i = 0; i < 4; i++) {
		int *c = cnt[i];
		c[255] = arraySize - c[255];
		for (int k = 254; k >= 0; k--)
			c[k] = c[k + 1] - c[k];
		assert(c[0] == 0);
	}
	// perform 4 passes over array data
	for (int i = 0; i < 4; i++) {
		int *c = cnt[i];
		int shift = (i * 8);
		for (int k = 0; k < arraySize; k++) {
			T value = array[k];
			int bucket = ((value >> shift) & 255);
			temp[c[bucket]++] = value;
		}
		T *tmp = temp;
		temp = array;
		array = tmp;
	}
#else
	int c[256];
	// perform 4 passes over array data
	for (int i = 0; i < 4; i++) {
		int shift = (i * 8);
		memset(c, 0, sizeof(c));
		for (int k = 0; k < arraySize; k++)
			c[((array[k]) >> shift) & 255]++;
		c[255] = arraySize - c[255];
		for (int k = 254; k >= 0; k--)
			c[k] = c[k + 1] - c[k];
		assert(c[0] == 0);
		for (int k = 0; k < arraySize; k++) {
			T value = array[k];
			int bucket = ((value >> shift) & 255);
			temp[c[bucket]++] = value;
		}
		T *tmp = temp;
		temp = array;
		array = tmp;
	}
#endif
}


void sort(T *array, int cnt, T *temp1, T *temp2) {
#if 0
	qsort(array, cnt, sizeof(T), cmp);
#elif 0
	mergeSortRecursive(array, cnt, temp1, temp2);
	memcpy(array, temp1, cnt * sizeof(T));
#elif 0
	mergeSortIterative(array, cnt, temp1);
#elif 0
	heapSort(array, cnt, array);
#elif 1
	radixSort(array, cnt, temp1);
#endif
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage:  sorting ARRAY_SIZE\n");
		exit(1);
	}
	int arraySize;
	assert(sscanf(argv[1], "%d", &arraySize) == 1);
	assert(arraySize > 1);
	printf("Sorting an array with %d elements (%d bytes in total).\n", arraySize, arraySize * sizeof(T));
	
	T *array = new T[arraySize];
	T *backup = new T[arraySize];
	T *sorted = new T[arraySize];
	T *temp1 = new T[arraySize];
	T *temp2 = new T[arraySize];

	for (int i = 0; i < arraySize; i++)
		array[i] = random() % 2000000000;
	memcpy(backup, array, arraySize * sizeof(T));
	memcpy(sorted, array, arraySize * sizeof(T));
	qsort(sorted, arraySize, sizeof(T), cmp);

	for (int i = 0; i < ITER; i++) {
		memcpy(array, backup, arraySize * sizeof(T));
		double startTime = getTime();
		sort(array, arraySize, temp1, temp2);
		double endTime = getTime();
		printf("Time elapsed: %.6lf seconds.\n", endTime - startTime);
		for (int i = 0; i < arraySize; i++)
			assert(array[i] == sorted[i]);
	}
	
	return 0;
}


