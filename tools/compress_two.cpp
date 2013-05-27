#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>


#define MIN(a, b) (a < b ? a : b)


long long list1[8 * 1024 * 1024];
long long list2[8 * 1024 * 1024];

int len1, len2;

int readFile(char *fileName, long long *array) {
	int result = 0;
	FILE *f = fopen(fileName, "r");
	assert(f != NULL);
	while (fscanf(f, "%lld", &array[result]) == 1)
		result++;
	fclose(f);
	return result;
}

int getBitCnt(long long l) {
	int result = 1;
	while (l > 1) {
		l >>= 1;
		result++;
	}
	return result;
}

int getVByteSize(long long l) {
	int result = 1;
	while (l >= 128) {
		l >>= 7;
		result++;
	}
	return result;
}


static int compressSimple(long long *list, int length) {
	int cnt[36];
	for (int i = 0; i < 36; i++)
		cnt[i] = 1;
	int totalCnt = 36;
	long long prev = -1;
	double bitCnt = 8 * (1 + getVByteSize(length));
	for (int i = 0; i < length; i++) {
		long long delta = list[i] - prev;
		prev = list[i];
		int bucket = getBitCnt(delta);
		double prob = cnt[bucket] * 1.0 / totalCnt;
		bitCnt += -log(prob) / log(2) + bucket - 1;
		cnt[bucket]++;
		totalCnt++;
	}
	int byteCnt = (int)((bitCnt + 7) / 8);
	return byteCnt;
}


static int getIntersection(long long *list1, int len1, long long *list2, int len2, long long *inter) {
	int ilen = 0;
	int pos1 = 0, pos2 = 0;
	while (true) {
		if (list1[pos1] < list2[pos2]) {
			if (++pos1 >= len1)
				break;
		}
		else if (list1[pos1] > list2[pos2]) {
			if (++pos2 >= len2)
				break;
		}
		else {
			if (inter != NULL)
				inter[ilen] = list1[pos1];
			ilen++;
			if ((++pos1 >= len1) || (++pos2 >= len2))
				break;
		}
	}
	return ilen;
}


static int factorOut(long long *list1, int len1, long long *list2, int len2) {
	long long *list1b = new long long[len1];
	long long *list2b = new long long[len2];
	long long *inter = new long long[MIN(len1, len2)];
	int len1b = 0, len2b = 0, ilen = 0;

	int pos1 = 0, pos2 = 0;
	while (true) {
		if (list1[pos1] < list2[pos2]) {
			list1b[len1b++] = list1[pos1++];
			if (pos1 >= len1)
				break;
		}
		else if (list1[pos1] > list2[pos2]) {
			list2b[len2b++] = list2[pos2++];
			if (pos2 >= len2)
				break;
		}
		else {
			inter[ilen++] = list1[pos1];
			pos1++;
			pos2++;
			if ((pos1 >= len1) || (pos2 >= len2))
				break;
		}
	}
	while (pos1 < len1)
		list1b[len1b++] = list1[pos1++];
	while (pos2 < len2)
		list2b[len2b++] = list2[pos2++];
	
	int result =
		compressSimple(list1b, len1b) + compressSimple(list2b, len2b) + compressSimple(inter, ilen);
	delete[] list1b;
	delete[] list2b;
	delete[] inter;
	return result;
}


static int compressByReference(
		long long *primaryList, int primaryLen, long long *refList, int refLen) {
#if 1
	long long lastCrossRef = -1;
	long long lastDocRef = -1;
	int crossRefCnt = 36, docRefCnt = 36;
	int bitCnt[36], bitCnt2[36], bitCnt3[36];
	for (int i = 0; i < 36; i++)
		bitCnt[i] = bitCnt2[i] = bitCnt3[i] = 1;
	double size = 8 * (1 + getVByteSize(primaryLen));

	long long prev = -1;
	int refPos = 0;
	for (int i = 0; i < primaryLen; i++) {
		while ((refPos < refLen) && (refList[refPos] < primaryList[i]))
			refPos++;
		if ((refPos < refLen) && (refList[refPos] == primaryList[i])) {
			size += -log(crossRefCnt * 1.0 / (docRefCnt + crossRefCnt)) / log(2);
			long long delta = refPos - lastCrossRef;
			assert(delta > 0);
			int bucket = getBitCnt(delta);
			size += -log(bitCnt2[bucket] * 1.0 / crossRefCnt) / log(2) + bucket - 1;
			bitCnt2[bucket]++;
			crossRefCnt++;
			lastCrossRef = refPos;
		}
		else {
			size += -log(docRefCnt * 1.0 / (docRefCnt + crossRefCnt)) / log(2);
			long long delta = primaryList[i] - prev;
			assert(delta > 0);
			int bucket = getBitCnt(delta);
			size += -log(bitCnt[bucket] * 1.0 / docRefCnt) / log(2) + bucket - 1;
			bitCnt[bucket]++;
			docRefCnt++;
			lastCrossRef = refPos - 1;
		}
		prev = primaryList[i];
	}
#else
	int bitCnt1[36], bitCnt2[36];
	for (int i = 0; i < 36; i++)
		bitCnt1[i] = bitCnt2[i] = 1;
	int totalCnt = 36;
	double size = 8 * (1 + getVByteSize(primaryLen));

	int previousRef = -1;
	int refPos = -1;
	for (int i = 0; i < primaryLen; i++) {
		while (refPos < refLen - 1) {
			if (refList[refPos + 1] <= primaryList[i])
				refPos++;
			else
				break;
		}

		long long directGap = (i == 0 ? primaryList[i] + 1 : primaryList[i] - primaryList[i - 1]);

		long long refGap = refPos - previousRef + 1;
		int refGapBucket = getBitCnt(refGap);

		long long indirectGap = directGap;
		if (refPos >= 0)
			indirectGap = primaryList[i] - refList[refPos] + 1;

		if (getBitCnt(directGap) <= getBitCnt(refGap) + getBitCnt(indirectGap)) {
			// encode zero-gap in reference list
			size += -log(bitCnt1[1] * 1.0 / totalCnt) / log(2);			
			bitCnt1[1]++;
			// encode gap between previous posting and current posting
			int bucket = getBitCnt(directGap);
			size += -log(bitCnt2[bucket] * 1.0 / totalCnt) / log(2) + bucket - 1;
			bitCnt2[bucket]++;
		}
		else {
			// encode gap in reference list
			int refBucket = getBitCnt(refGap);
			size += -log(bitCnt1[refBucket] * 1.0 / totalCnt) / log(2);
			bitCnt1[refBucket]++;
			// encode gap between reference posting and current posting
			int bucket = getBitCnt(indirectGap);
			size += -log(bitCnt2[bucket] * 1.0 / totalCnt) / log(2) + bucket - 1;
			bitCnt2[bucket]++;
		}

		previousRef = refPos;
		totalCnt++;
	}
#endif
	int byteCnt = (int)((size + 7) / 8);
	return byteCnt;
}


int main(int argc, char **argv) {
	assert(argc == 3);
	len1 = readFile(argv[1], list1);
	len2 = readFile(argv[2], list2);
	printf("Lists read: %d/%d.\n", len1, len2);
	printf("Size of intersection: %d.\n\n", getIntersection(list1, len1, list2, len2, NULL));

	int blen1 = compressSimple(list1, len1);
	printf("Compressing list 1 (old method): %.2lf bits/posting. %d bytes in total.\n",
			blen1 * 8.0 / len1, blen1);

	int blen2 = compressSimple(list2, len2);
	printf("Compressing list 2 (old method): %.2lf bits/posting. %d bytes in total.\n",
			blen2 * 8.0 / len2, blen2);

	printf("Sum of list 1 and list 2: %.2lf bits/posting. %d bytes in total.\n\n",
			(blen1 + blen2) * 8.0 / (len1 + len2), blen1 + blen2);

	int blen3 = factorOut(list1, len1, list2, len2);
	printf("Factoring out the intersection: %.2lf bits/posting. %d bytes in total.\n\n",
			blen3 * 8.0 / (len1 + len2), blen3);

	int blen4 = compressByReference(list1, len1, list2, len2);
	printf("Compressing list 1 by reference to list 2: %.2lf bits/posting. %d bytes in total.\n",
			blen4 * 8.0 / len1, blen4);

	int blen5 = compressByReference(list2, len2, list1, len1);
	printf("Compressing list 2 by reference to list 1: %.2lf bits/posting. %d bytes in total.\n",
			blen5 * 8.0 / len2, blen5);

	return 0;
}


