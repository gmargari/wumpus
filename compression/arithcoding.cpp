#include <stdio.h>
#include <stdlib.h>
#include "arithcoding.h"
#include "frequency_tree.h"
#include "../index/index_compression.h"


byte * arithEncode(int *uncompressed, int listLength, bool semiStatic, int *byteLength) {
	// compute min and max values so that we can create an appropriate
	// frequency tree; also collect statistics in case a semi-static encoding
	// process has been requested
	int min = 999999999, max = -999999999;
	for (int i = 0; i < listLength; i++) {
		if (uncompressed[i] < min)
			min = uncompressed[i];
		if (uncompressed[i] > max)
			max = uncompressed[i];
	}
	if ((min < -1000000) || (max > 1000000) || (max - min > 1000000)) {
		*byteLength = -1;
		return NULL;
	}

	// initialize frequency values used for defining the intervals
	FrequencyTree tree(min, max);
	tree.reset(1);
	if (semiStatic) {
		tree.reset(0);
		for (int i = 0; i < listLength; i++)
			tree.increaseFrequency(uncompressed[i], 1);
	}
	byte *result = (byte*)malloc(4 * listLength + 16);
	int outPos = 0;

	outPos += encodeVByte32(listLength, &result[outPos]);
	outPos += encodeVByte32(min, &result[outPos]);
	outPos += encodeVByte32(max - min, &result[outPos]);

	// initialize output buffer
	byte bitBuffer = (semiStatic ? 1 : 0);
	int bitsInBuffer = 1;

	// we are going to approximate the real numbers between 0 and 1 by integers
	// between 0 and MAX
	static const int MAX = (1 << 30);

	// maintain two intervals: one for the interval to be encoded, another one
	// for the interval encoded so far; these intervals will be re-scaled after
	// each iteration, in order to keep the approximation error small
	int low = 0, high = MAX;
	int encodedLow = 0, encodedHigh = MAX;

	// encode all elements in the list
	for (int i = 0; i < listLength; i++) {
		int current = uncompressed[i];

		// extract probability interval for element "current" from the frequency tree
		double lower = tree.getCumulativeFrequency(current) * 1.0 / tree.getTotalFrequency();
		double upper = tree.getCumulativeFrequency(current - 1) * 1.0 / tree.getTotalFrequency();
		if (!semiStatic)
			tree.increaseFrequency(current, 1);

		int range = high - low;
		high = low + (int)(range * lower);
		low = low + (int)(range * upper);
		int powerLost = 0;

		while (true) {
			int middle = (encodedLow + encodedHigh) >> 1;
			int bit = 0;
			if (high <= middle)
				encodedHigh = middle;
			else if (low >= middle) {
				encodedLow = middle;
				bit = 1;
			}
			else
				break;
			bitBuffer |= (bit << bitsInBuffer);
			if (++bitsInBuffer >= 8) {
				result[outPos++] = bitBuffer;
				bitsInBuffer = 0;
			}
			powerLost++;
		}

		// re-scale intervals to avoid rounding problems
		if (powerLost > 0) {
			low = (low - encodedLow) << powerLost;
			high = (high - encodedLow) << powerLost;
			encodedLow = 0;
			encodedHigh = MAX;
		}
	}
	result[outPos++] = bitBuffer;

	*byteLength = outPos;
	return result;
} // end of arithEncode(int*, int, bool, int*)


int * arithDecode(byte *compressed, int *listLength) {
} // end of arithDecode(byte*, int*)


int main() {
	int test[1000000];
	for (int i = 0; i < 1000000; i++)
		test[i] = random() % 32;
	int byteLength;
	arithEncode(test, 1000000, false, &byteLength);
	printf("byteLength == %d\n", byteLength);
	return 0;
}


