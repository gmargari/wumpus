/**
 * This program processes a positionless document-level Wumpus index file
 * (index.NNN) and tests several compression algorithms on it.
 *
 * Usage:  compress_by_reference INDEX_FILE
 *
 * author: Stefan Buettcher
 * created: 2006-12-20
 * changed: 2006-12-20
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "../filters/trec_inputstream.h"
#include "../index/compactindex.h"
#include "../index/index_iterator.h"
#include "../misc/all.h"


using namespace std;

struct TermDescriptor {
	char term[32];
	bool used;
	int df, byteLen;
	byte *postings;
	int next;
};


static const int HASHTABLE_SIZE = 167953;
int32_t hashtable[HASHTABLE_SIZE];
vector<TermDescriptor> dictionary;

uint16_t *forwardIndex;
int forwardIndexSize = 0;
int *documentPositions;
int termCount = 0;
int documentCount = 0;


/** Ignore all terms that have less than this many postings. **/
static const int MIN_POSTINGS = 64;


static void processIndexFile(char *fileName) {
	int fd = open(fileName, O_RDONLY);
	assert(fd >= 0);
	close(fd);

	char previousTerm[32];
	previousTerm[0] = 0;

	offset *postings = new offset[26000000];
	int pCnt = 0;

	IndexIterator *iter = CompactIndex::getIterator(fileName, 4 * 1024 * 1024);
	while (iter->hasNext()) {
		char currentTerm[32];
		strcpy(currentTerm, iter->getNextTerm());
		if (strncmp(currentTerm, "<!>", 3) == 0)
			memmove(currentTerm, &currentTerm[3], sizeof(currentTerm) - 3);
		if (strcmp(currentTerm, previousTerm) != 0) {
			if ((pCnt >= MIN_POSTINGS) && (previousTerm[0] != '<')) {
				TermDescriptor td;
				strcpy(td.term, previousTerm);
				td.df = pCnt;
				td.used = false;
#if 0
				for (int i = 0; i < pCnt; i++) {
					postings[i] >>= DOC_LEVEL_SHIFT;
					if (i > 0)
						assert(postings[i] > postings[i - 1]);
				}
#endif
				td.postings = compressVByte(postings, pCnt, &td.byteLen);
				dictionary.push_back(td);
			}
			else if (strcmp(previousTerm, "<doc>") == 0)
				documentCount = pCnt;
			pCnt = 0;
			strcpy(previousTerm, currentTerm);
		}
		int length;
		iter->getNextListUncompressed(&length, &postings[pCnt]);
		assert(length > 0);
		pCnt += length;
	}
	if ((pCnt >= MIN_POSTINGS) && (previousTerm[0] != '<'))	{
		TermDescriptor td;
		strcpy(td.term, previousTerm);
		td.df = pCnt;
		td.used = false;
		td.postings = compressVByte(postings, pCnt, &td.byteLen);
		dictionary.push_back(td);
	}
	else if (strcmp(previousTerm, "<doc>") == 0)
		documentCount = pCnt;

	delete iter;
	delete[] postings;
	assert(documentCount > 1);
} // end of processIndexFile(char*)


static void buildForwardIndex() {
	documentPositions = new int[documentCount + 1];
	memset(documentPositions, 0, documentCount * sizeof(int));
	for (int i = 0; i < termCount; i++) {
		int len;
		offset *list = decompressList(dictionary[i].postings, dictionary[i].byteLen, &len, NULL);
		for (int k = 0; k < len; k++)
			documentPositions[list[k]]++;
		free(list);
	}

	forwardIndexSize = 0;
	for (int i = 0; i < documentCount; i++) {
		forwardIndexSize += documentPositions[i];
		documentPositions[i] = forwardIndexSize - documentPositions[i];
	}
	documentPositions[documentCount] = forwardIndexSize;
	forwardIndex = new uint16_t[forwardIndexSize];

	int *docPos = new int[documentCount];
	memcpy(docPos, documentPositions, documentCount * sizeof(int));
	for (int i = 0; i < termCount; i++) {
		int len;
		offset *list = decompressList(dictionary[i].postings, dictionary[i].byteLen, &len, NULL);
		for (int k = 0; k < len; k++) {
			int docid = list[k];
			forwardIndex[docPos[docid]++] = i;
		}
		free(list);
	}
	for (int i = 0; i < documentCount; i++)
		assert(docPos[i] == documentPositions[i + 1]);
	delete[] docPos;
} // end of buildForwardIndex()


static inline int getTermID(const char *term) {
	unsigned int slot = simpleHashFunction(term) % HASHTABLE_SIZE;
	for (int runner = hashtable[slot]; runner >= 0; runner = dictionary[runner].next)
		if (strcmp(term, dictionary[runner].term) == 0)
			return runner;
	return -1;
}


bool compareTwoTerms(const TermDescriptor &t1, const TermDescriptor &t2) {
//	return (strcmp(t1.term, t2.term) <= 0);
	return (t1.df >= t2.df);
}


int compareTermsByDF(const void *a, const void *b) {
	TermDescriptor *x = (TermDescriptor*)a;
	TermDescriptor *y = (TermDescriptor*)b;
	return y->df - x->df;
}


void getIntersection(offset *list1, int len1, offset *list2, int len2, int *length, offset *inter) {
	offset *result = new offset[MIN(len1, len2) + 1];
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
	*length = ilen;
} // end of getIntersection(...)


double getSavings(int primary, int reference, double *oldCost, double *newCost) {
	double N = documentCount;
	double L = dictionary[primary].df;
	double R = dictionary[reference].df;

	int dummy;
	offset *list1 = decompressList(
			dictionary[primary].postings, dictionary[primary].byteLen, &dummy, NULL);
	offset *list2 = decompressList(
			dictionary[reference].postings, dictionary[reference].byteLen, &dummy, NULL);
	getIntersection(list1, dictionary[primary].df, list2, dictionary[reference].df, &dummy, NULL);
	double I = dummy;
	free(list1);
	free(list2);

	*oldCost = L * log(N/L+1) / log(2);
	*newCost =
		(L-I) * log(N/L+1) / log(2) + I * log(R/L+1) / log(2) + (L-I) * log(L/(L-I)) / log(2) + I * log(L/I) / log(2);
	return *oldCost - *newCost;
} // end of getSavings(int, int)


void getCandidateTerms(offset *documents, int len, int termID, map<int,double> *result) {
	result->clear();
	int iterations = 0;
	for (int i = 0; i < 50; i++) {
		int doc;
		do {
			doc = documents[random() % len];
		} while ((doc >= documentCount) || (documentPositions[doc] >= documentPositions[doc + 1]));
		uint16_t *termArray = &forwardIndex[documentPositions[doc]];
		int termCnt = documentPositions[doc + 1] - documentPositions[doc];
		for (int k = 0; k < termCnt; k++) {
			if ((termArray[k] != termID) && (!dictionary[termArray[k]].used)) {
				if (result->find(termArray[k]) == result->end())
					(*result)[termArray[k]] = 1;
				else
					(*result)[termArray[k]]++;
			}
		}
		iterations++;
	}
	for (map<int,double>::iterator iter = result->begin(); iter != result->end(); ++iter)
		iter->second /= iterations;
} // end of getCandidateTerms(offset*, int, map<int,double>*)


void testPairing(const char *compressionMethod) {
	long long totalSizeOriginal = 0;
	long long totalSizeNew = 0;

	Compressor compressor = compressorForID[getCompressorForName(compressionMethod)];

	for (int p = 0; p < termCount; p++)
		dictionary[p].used = false;

	for (int p = 0; p < termCount; p++) {
		if (p % 1000 == 0)
			printf("%d/%d terms done\n", p, termCount);

		// decompress postings for current term
		int len, byteLen;
		offset *uncompressed =
			decompressList(dictionary[p].postings, dictionary[p].byteLen, &len, NULL);
		assert(len == dictionary[p].df);

		// recompress to find out the size of the original list
		free(compressor(uncompressed, len, &byteLen));
		totalSizeOriginal += byteLen;

		if ((dictionary[p].df < 16) || (dictionary[p].used))
			continue;

		// find best matching term to build a pair
		map<int,double> candidates;
		getCandidateTerms(uncompressed, len, p, &candidates);
		int best = -1;
		double bestScore = 0.05;
		for (map<int,double>::iterator iter = candidates.begin(); iter != candidates.end(); ++iter) {
			double P = dictionary[p].df;
			double R = dictionary[iter->first].df;
			double I = P * iter->second;
			double N = documentCount;
			if ((P < 16) || (R < 16) || (I < 16) || (P > N * 0.8) || (R > N * 0.8))
				continue;
			double oldCost = P * log(N/P + 1) + R * log(N/R + 1);
			double newCost = I * log(N/I + 1) + (P-I) * log(N/(P-I) + 1) + (R-I) * log(N/(R-I) + 1);
			double score = (oldCost - newCost) / (P + R);
			if (score > bestScore) {
				best = iter->first;
				bestScore = score;
			}
		}

		if (best >= 0) {
			assert(best != p);
printf("Pair found: \"%s\"/\"%s\": %.2lf bits/posting saved.\n",
dictionary[p].term, dictionary[best].term, bestScore);
			// factor out the intersection of the two posting lists
			int len1, len2;
			offset *list1 = decompressList(dictionary[p].postings, dictionary[p].byteLen, &len1, NULL);
			offset *list2 = decompressList(dictionary[best].postings, dictionary[best].byteLen, &len2, NULL);
			offset *inter = typed_malloc(offset, MIN(len1, len2));
			int pos1 = 0, pos2 = 0, ilen = 0, outLen1 = 0, outLen2 = 0;
			while (true) {
				if (list1[pos1] < list2[pos2]) {
					list1[outLen1++] = list1[pos1++];
					if (pos1 >= len1)
						break;
				}
				else if (list2[pos2] < list1[pos1]) {
					list2[outLen2++] = list2[pos2++];
					if (pos2 >= len2)
						break;
				}
				else {
					inter[ilen++] = list1[pos1];
					pos1++; pos2++;
					if ((pos1 >= len1) || (pos2 >= len2))
						break;
				}
			}
			while (pos1 < len1)
				list1[outLen1++] = list1[pos1++];
			while (pos2 < len2)
				list2[outLen2++] = list2[pos2++];

			// compress list1 \ inter, list2 \ inter, and inter itself
			int blen1, blen2, blen3;
			free(compressor(list1, outLen1, &blen1));
			free(compressor(list2, outLen2, &blen2));
			free(compressor(inter, ilen, &blen3));
			free(list1);
			free(list2);
			free(inter);

			totalSizeNew += blen1 + blen2 + blen3 + 2 * 4;
			dictionary[p].used = dictionary[best].used = true;
			if (p % 1000 == 0)
				printf("  \"%s\"/\"%s\"\n", dictionary[p].term, dictionary[best].term);
		}
		
		free(uncompressed);
	}

	// compress all terms that have not been covered so far using the old-fashioned
	// compression method
	for (int p = 0; p < termCount; p++)
		if (!dictionary[p].used) {
			int len, byteLen;
			offset *uncompressed =
				decompressList(dictionary[p].postings, dictionary[p].byteLen, &len, NULL);
			assert(len == dictionary[p].df);
			free(compressor(uncompressed, len, &byteLen));
			totalSizeNew += byteLen;
			free(uncompressed);
		}

	printf("Original size (compressing each list individually): %lld bytes.\n", totalSizeOriginal);
	printf("New size (pairing terms, factoring out intersection): %lld bytes.\n", totalSizeNew);
} // end of testPairing(char*)


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


int codeArithmetic(offset *list, int len) {
	int direct[32];
	int bucket[32];
	int totalCnt = 0;
	for (int i = 1; i < 8; i++) {
		direct[i] = 1;
		totalCnt++;
	}
	for (int i = 4; i < 32; i++) {
		bucket[i] = 1;
		totalCnt++;
	}

	double size = 8 * (1 + getVByteSize(len) + getVByteSize(list[0] + 1));

	for (int i = 1; i < len; i++) {
		offset delta = list[i] - list[i - 1];
		if (delta < 8) {
			size += log(totalCnt * 1.0 / direct[delta]);
			direct[delta]++;
		}
		else {
			int bits = getBitCnt(delta);
			size += log(totalCnt * 1.0 / bucket[bits]) / log(2);
			size += bits - 1;
			bucket[bits]++;
		}
		totalCnt++;
	}

	return (int)((size + 7) / 8);
}


int codeArithmeticByRef(offset *list, int len, offset *refList, int refLen) {
	int direct[32], refDirect[32];
	int bucket[32], refBucket[32];
	int totalCnt = 0, totalRefCnt = 0;
	for (int i = 1; i < 8; i++) {
		direct[i] = refDirect[i] = 1;
		totalCnt++;
		totalRefCnt++;
	}
	for (int i = 4; i < 32; i++) {
		bucket[i] = refBucket[i] = 1;
		totalCnt++;
		totalRefCnt++;
	}

	double size = 8 * (1 + getVByteSize(len) + getVByteSize(list[0] + 1)) + 32;

	int prevRef = -1, refPos = 0;
	for (int i = 1; i < len; i++) {
		while ((refPos + 1 < refLen) && (refList[refPos + 1] <= list[i]))
			refPos++;
		if ((refPos < refLen) && (refList[refPos] == list[i])) {
			size += log((totalCnt + totalRefCnt) * 1.0 / totalRefCnt) / log(2);
			if (refPos < prevRef + 8) {
				size += log(totalRefCnt * 1.0 / refDirect[refPos - prevRef]) / log(2);
				refDirect[refPos - prevRef]++;
			}
			else {
				int bits = getBitCnt(refPos - prevRef);
				size += log(totalRefCnt * 1.0 / refBucket[bits]) / log(2);
				size += bits - 1;
				refBucket[bits]++;
			}
			totalRefCnt++;
		}
		else {
			size += log((totalCnt + totalRefCnt) * 1.0 / totalCnt) / log(2);
			offset delta = list[i] - list[i - 1];
			if (delta < 8) {
				size += log(totalCnt * 1.0 / direct[delta]) / log(2);
				direct[delta]++;
			}
			else {
				int bits = getBitCnt(delta);
				size += log(totalCnt * 1.0 / bucket[bits]) / log(2);
				size += bits - 1;
				bucket[bits]++;
			}
			totalCnt++;
		}
		prevRef = refPos;
	}

	return (int)((size + 7) / 8);
}


void testRecursive(const char *compressionMethod) {
	long long totalSizeOriginal = 0;
	long long totalSizeNew = 0;

	Compressor compressor = compressorForID[getCompressorForName(compressionMethod)];

	for (int p = 0; p < termCount; p++)
		dictionary[p].used = false;

	for (int p = 0; p < termCount; p++) {
		if (p % 100 == 0)
			printf("%d/%d terms done. totalSizeOriginal = %lld, totalSizeNew = %lld.\n",
					p, termCount, totalSizeOriginal, totalSizeNew);

		// decompress postings for current term
		int len, byteLen;
		offset *uncompressed =
			decompressList(dictionary[p].postings, dictionary[p].byteLen, &len, NULL);
		assert(len == dictionary[p].df);

		// recompress to find out the size of the original list
		totalSizeOriginal += codeArithmetic(uncompressed, len);

		// find best matching term to build a pair
		map<int,double> candidates;
		getCandidateTerms(uncompressed, len, p, &candidates);
		int best = -1;
		double bestScore = 0.1;
		for (map<int,double>::iterator iter = candidates.begin(); iter != candidates.end(); ++iter) {
			if (iter->first >= p)
				continue;

			double P = dictionary[p].df;
			double R = dictionary[iter->first].df;
			double I = P * iter->second;
			double N = documentCount;
			if ((P < 16) || (R < 16) || (I < 16) || (P > N * 0.8))
				continue;

			double oldCost = P * log(N/P + 1);
			double newCost = (P-I) * log(N/P+1) / log(2)
			               + I * log(R/P+1) / log(2)
			               + (P-I) * log(P/(P-I)) / log(2)
			               + I * log(P/I) / log(2);
			double score = (oldCost - newCost) / P;
			if (score > bestScore) {
				best = iter->first;
				bestScore = score;
			}
		}

		if (best < 0)
			totalSizeNew += codeArithmetic(uncompressed, len);
		else {
			printf("Compressing \"%s\" via \"%s\".\n", dictionary[p].term, dictionary[best].term);
			int refLen;
			offset *refList =
				decompressList(dictionary[best].postings, dictionary[best].byteLen, &refLen, NULL);
			assert(refLen == dictionary[best].df);
			totalSizeNew += codeArithmeticByRef(uncompressed, len, refList, refLen);
			free(refList);
		}

		free(uncompressed);
	}

	printf("Original size (compressing each list individually): %lld bytes.\n", totalSizeOriginal);
	printf("New size (pairing terms, factoring out intersection): %lld bytes.\n", totalSizeNew);
} // end of testRecursive(char*)


int main(int argc, char **argv) {
	if ((argc < 2) || (argc > 3)) {
		fprintf(stderr, "Usage:  compress_by_reference INDEX_FILE [COMPRESSION_METHOD]\n\n");
		return 1;
	}

	processIndexFile(argv[1]);
	printf("Index processed. %d terms with sufficiently long lists found.\n", dictionary.size());

	termCount = dictionary.size();
	TermDescriptor *tds = new TermDescriptor[termCount];
	for (int i = 0; i < termCount; i++)
		tds[i] = dictionary[i];
	qsort(tds, termCount, sizeof(TermDescriptor), compareTermsByDF);
	dictionary.clear();
	for (int i = 0; i < termCount; i++)
		dictionary.push_back(tds[i]);
	delete[] tds;
	printf("Terms sorted by DF.\n");
	if (termCount >= 65536) {
		printf("Too many terms in index. Restricting to top 65535.\n");
		termCount = 65535;
	}
		

	buildForwardIndex();
	printf("Forward index built from inverted file.\n");
	
	for (int i = 0; i < HASHTABLE_SIZE; i++)
		hashtable[i] = -1;
	for (int i = 0; i < termCount; i++) {
		int slot = simpleHashFunction(dictionary[i].term) % HASHTABLE_SIZE;
		dictionary[i].used = false;
		dictionary[i].next = hashtable[slot];
		hashtable[slot] = i;
	}
	int maxChain = 0;
	for (int i = 0; i < HASHTABLE_SIZE; i++) {
		int cnt = 0;
		for (int runner = hashtable[i]; runner >= 0; runner = dictionary[runner].next)
			cnt++;
		if (cnt > maxChain)
			maxChain = cnt;
	}
	printf("Hashtable constructed. Longest chain: %d.\n", maxChain);

#if 1
	testPairing(argc == 3 ? argv[2] : "vbyte");
#else
	testRecursive(argc == 3 ? argv[2] : "vbyte");
#endif

	return 0;
} // end of main(int, char**)


