/**
 * This program reads a sequence of TREC-formatted documents from stdin and
 * prints summary information regarding the lengths of the tokens to stdout.
 *
 * author: Stefan Buettcher
 * created: 2006-12-15
 * changed: 2006-12-15
 **/


#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


int maxLen = 0;
int bitCounts[40];

static inline int getBitCount(int n) {
	int result = 1;
	while (n > 1) {
		n >>= 1;
		result++;
	}
	return result;
}


static inline void processCurrentToken(int curLen) {
	if (curLen > maxLen)
		maxLen = curLen;
	if (curLen > 0)
		bitCounts[getBitCount(curLen)]++;
}


int main() {
	memset(bitCounts, 0, sizeof(bitCounts));
	char buffer[2048];
	double done = 0, prevDone = 0;
	int n, curLen = 0;
	while ((n = fread(buffer, 1, 1024, stdin)) > 0) {
		done += n;
		if (done > prevDone + pow(2, 30)) {
			printf("%.1lf MB read\n", done / pow(2, 20));
			prevDone = done;
			printf("maxLen = %d\n", maxLen);
			for (int i = 1; i < 20; i++)
				printf("bitCounts[%2d] = %9d\n", i, bitCounts[i]);
		}
		for (int i = 0; i < n; i++) {
			if (buffer[i] == '<') {
				processCurrentToken(curLen);
				curLen = 1;
				if (buffer[i + 1] == '/') {
					curLen = 2;
					i++;
				}
			}
			else if (buffer[i] == '>') {
				curLen++;
				processCurrentToken(curLen);
				curLen = 0;
			}
			else if (((buffer[i] | 32) >= 'a') && ((buffer[i] | 32) <= 'z'))
				curLen++;
			else if ((buffer[i] >= '0') && (buffer[i] <= '9'))
				curLen++;
//			else if (buffer[i] < 0)
//				curLen++;
			else {
				processCurrentToken(curLen);
				curLen = 0;
			}
		}
	}
	processCurrentToken(curLen);
	printf("maxLen = %d\n", maxLen);
	for (int i = 1; i < 20; i++)
		printf("bitCounts[%2d] = %9d\n", i, bitCounts[i]);
	return 0;
}


