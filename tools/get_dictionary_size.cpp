/**
 * This program processes a Wumpus index file (index.NNN) and outputs the size
 * that a naive in-memory dictionary would consume.
 *
 * Usage:  get_dictionary_size INDEX_FILE COMPRESSION_METHOD GROUP_SIZE > OUTPUT_FILE
 *
 * COMPRESSION_METHOD is one of: NONE, FRONTCODING, LZW, BOTH
 *
 * GROUP_SIZE is an integer, indicating how many dictionary entries should be
 * grouped together when applying compression.
 *
 * author: Stefan Buettcher
 * created: 2006-11-29
 * changed: 2007-07-02
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../index/compactindex.h"
#include "../misc/utils.h"


#define USE_VBYTE 1


static const int COMPRESSION_FRONTCODING = 111;
static const int COMPRESSION_LZW = 222;
static const int COMPRESSION_BOTH = COMPRESSION_FRONTCODING + COMPRESSION_LZW;

static int compressionMethod = COMPRESSION_NONE;

static long long indexBlockSize = 0;
static int groupSize = 1;

struct TermDescriptor {
	char term[32];
	int64_t filePointer;
};

static TermDescriptor currentGroup[1024];
static int currentGroupSize = 0;

static int32_t *arrayPointers;
static int groupCount = 0;
static char *termArray;
static int termArraySize = 0;

static const int BUFFER_SIZE = 1024 * 1024;
static char readBuffer[BUFFER_SIZE];
static int bufferSize = 0, bufferPos = 0;
static char *randomTerms[100000];

static int fd;


static int compress(char *firstTerm, int64_t firstPointer, TermDescriptor *data, byte *compressed) {
	byte largeBuffer[65536];
	byte buffer[8192];
	uLongf destLen;
	if (groupSize <= 1)
		return 0;
	int result = 0;
	switch (compressionMethod) {
		case COMPRESSION_NONE:
			for (int i = 0; i < groupSize - 1; i++) {
				strcpy((char*)&compressed[result], data[i].term);
				result += strlen(data[i].term) + 1;
				*((int64_t*)&compressed[result]) = data[i].filePointer;
				result += sizeof(int64_t);
			}
			return result;
		case COMPRESSION_FRONTCODING:
			for (int i = 0; i < groupSize - 1; i++) {
				char *currentTerm = data[i].term;
				int64_t currentPointer = data[i].filePointer;

				// front-code the current term
				int match = 0;
				for (int k = 0; (currentTerm[k] != 0) && (currentTerm[k] == firstTerm[k]); k++)
					match++;
				if (match > 15)
					match = 15;
				int len = strlen(currentTerm);
				if (len - match <= 15) {
					compressed[result++] = len - match + (match << 4);
					for (int i = match; i < len; i++)
						compressed[result++] = currentTerm[i];
				}
				else {
					compressed[result++] = (match << 4);
					int i = match;
					while (compressed[result++] = (byte)currentTerm[i++]);
				}
				firstTerm = currentTerm;
#if USE_VBYTE
				// vbyte-code the current file pointer
				int64_t delta = currentPointer - firstPointer;
				assert(delta > 0);
				while (delta >= 128) {
					compressed[result++] = ((delta & 127) | 128);
					delta >>= 7;
				}
				compressed[result++] = delta;
				firstPointer = currentPointer;
#else
				*((int64_t*)&compressed[result]) = currentPointer;
				result += sizeof(offset);
#endif
			}
			return result;
		case COMPRESSION_LZW:
			compressionMethod = COMPRESSION_NONE;
			result = compress(firstTerm, firstPointer, data, buffer);
			compressionMethod = COMPRESSION_LZW;
			destLen = 65536;
			if (compress((Bytef*)compressed, &destLen, (Bytef*)buffer, (uLong)result) == Z_OK)
				return (int)destLen;
			assert(false);
		case COMPRESSION_BOTH:
			compressionMethod = COMPRESSION_FRONTCODING;
			result = compress(firstTerm, firstPointer, data, buffer);
			compressionMethod = COMPRESSION_BOTH;
			destLen = 65536;
			if (compress((Bytef*)compressed, &destLen, (Bytef*)buffer, (uLong)result) == Z_OK)
				return (int)destLen;
			assert(false);
	}
	assert(false);
} // end of compress(...)


static void decompress(char *firstTerm, int64_t firstPointer, byte *data, int length, TermDescriptor *result) {
	byte buffer[8192];
	if (groupSize <= 1)
		return;
	int pos = 0; uLongf destLen;
	switch (compressionMethod) {
		case COMPRESSION_NONE:
			for (int i = 0; i < groupSize - 1; i++) {
				char *c = result[i].term;
				while (*(c++) = (char)data[pos++]);
				result[i].filePointer = *((int64_t*)&data[pos]);
				pos += sizeof(int64_t);
			}
			return;
		case COMPRESSION_FRONTCODING:
			for (int i = 0; i < groupSize - 1; i++) {
				// front-decode the current term
				int match = (data[pos] >> 4);
				int remainder = (data[pos] & 7);
				pos++;
				char *c = result[i].term;
				for (int k = 0; k < match; k++)
					*(c++) = firstTerm[k];
				if (remainder > 0) {
					for (int k = match; k < match + remainder; k++)
						*(c++) = (char)data[pos++];
					*c = 0;
				}
				else {
					while (*(c++) = (char)data[pos++]);
				}
				firstTerm = result[i].term;
#if USE_VBYTE
				// vbyte-decode the current file pointer
				int64_t delta = 0;
				int shift = 0;
				do {
					int64_t dummy = (data[pos] & 127);
					delta += (dummy << shift);
					shift += 7;
				} while (data[pos++] & 128);
				result[i].filePointer = firstPointer + delta;
				firstPointer = result[i].filePointer;
#else
				result[i].filePointer = *((int64_t*)&data[pos]);
				pos += sizeof(int64_t);
#endif
			}
			return;
		case COMPRESSION_LZW:
			destLen = 65536;
			if (uncompress((Bytef*)buffer, &destLen, (Bytef*)data, (uLong)length) != Z_OK)
				assert(false);
			compressionMethod = COMPRESSION_NONE;
			decompress(firstTerm, firstPointer, buffer, (int)destLen, result);
			compressionMethod = COMPRESSION_LZW;
			return;
		case COMPRESSION_BOTH:
			destLen = 65536;
			if (uncompress((Bytef*)buffer, &destLen, (Bytef*)data, (uLong)length) != Z_OK)
				assert(false);
			compressionMethod = COMPRESSION_FRONTCODING;
			decompress(firstTerm, firstPointer, buffer, (int)destLen, result);
			compressionMethod = COMPRESSION_BOTH;
			return;
	}
	assert(false);
} // end of decompress(...)


static void getGroup(int g, TermDescriptor *descriptors) {
	assert(g >= 0);
	assert(g < groupCount);
	int end = termArraySize;
	if (g < groupCount - 1)
		end = arrayPointers[g + 1];
	int pos = arrayPointers[g];
	strcpy(descriptors[0].term, &termArray[pos]);
	pos += strlen(descriptors[0].term) + 1;
	memcpy(&descriptors[0].filePointer, &termArray[pos], sizeof(int64_t));
	pos += sizeof(int64_t);
	decompress(descriptors[0].term, descriptors[0].filePointer,
			(byte*)&termArray[pos], end - pos, &descriptors[1]);
} // end of getGroup(int, TermDescriptor*)


static void getRandomTerm(char *term) {
	TermDescriptor descriptors[1024];
	int g = random() % groupCount;
	getGroup(g, descriptors);
	int t = random() % groupSize;
	strcpy(term, descriptors[t].term);
} // end of getRandomTerm(char*)


static void ensureCacheIsFull(int bytesNeeded) {
	if (bufferSize < BUFFER_SIZE)
		return;
	if (bufferPos + bytesNeeded <= bufferSize)
		return;
	bufferSize -= bufferPos;
	memmove(readBuffer, &readBuffer[bufferPos], bufferSize);
	bufferPos = 0;
	int result = read(fd, &readBuffer[bufferSize], BUFFER_SIZE - bufferSize);
	if (result > 0)
		bufferSize += result;
} // end of ensureCacheIsFull(int)


static void addToGroup(char *term, int64_t filePos) {
	int len = strlen(term);
	strcpy(currentGroup[currentGroupSize].term, term);
	currentGroup[currentGroupSize].filePointer = filePos;
	currentGroupSize++;

	if (currentGroupSize >= groupSize) {
		arrayPointers[groupCount++] = termArraySize;
		strcpy(&termArray[termArraySize], term);
		termArraySize += len + 1;
		memcpy(&termArray[termArraySize], &filePos, sizeof(int64_t));
		termArraySize += sizeof(int64_t);
		termArraySize += compress(currentGroup[0].term, currentGroup[0].filePointer,
				&currentGroup[1], (byte*)&termArray[termArraySize]);
		currentGroupSize = 0;
	}
} // end of addToGroup(char*, int64_t)


static void processIndexFile(char *fileName) {
	fd = open(fileName, O_RDONLY);
	if (fd < 0)
		perror(fileName);
	assert(fd >= 0);
	struct stat buf;
	int status = fstat(fd, &buf);
	assert(status == 0);

	CompactIndex_Header header;
	lseek(fd, buf.st_size - sizeof(header), SEEK_SET);
	status = read(fd, &header, sizeof(header));
	assert(status == sizeof(header));
	int listCount = header.listCount;
	int termCount = header.termCount;
	lseek(fd, (off_t)0, SEEK_SET);

	bufferSize = read(fd, readBuffer, BUFFER_SIZE);
	bufferPos = 0;
	int listPos = 0;
	int64_t filePos = 0;
	currentGroupSize = 0;
	char previousTerm[256];
	previousTerm[0] = 0;

	long long filePosOfLastTerm = -999999999;
	long totalTermLength = 0;

	while (listPos < listCount) {
		char currentTerm[256];
		int64_t oldFilePos = filePos;
		ensureCacheIsFull(16384);
		strcpy(currentTerm, &readBuffer[bufferPos]);
		int len = strlen(currentTerm);
		totalTermLength += len;
		bufferPos += len + 1;
		filePos += len + 1;
		int32_t currentSegmentCount;
		memcpy(&currentSegmentCount, &readBuffer[bufferPos], sizeof(int32_t));
		bufferPos += sizeof(int32_t);
		filePos += sizeof(int32_t);
		PostingListSegmentHeader currentHeaders[CompactIndex::MAX_SEGMENTS_IN_MEMORY];
		memcpy(currentHeaders, &readBuffer[bufferPos],
				currentSegmentCount * sizeof(PostingListSegmentHeader));
		bufferPos += currentSegmentCount * sizeof(PostingListSegmentHeader);
		filePos += currentSegmentCount * sizeof(PostingListSegmentHeader);

		// add current term to in-memory dictionary, but only if it does not fit
		// into the previous block any more
		if (filePos > filePosOfLastTerm + indexBlockSize) {
			addToGroup(currentTerm, oldFilePos);
			filePosOfLastTerm = oldFilePos;
		}

		for (int k = 0; k < currentSegmentCount; k++) {
			int byteSize = currentHeaders[k].byteLength;
			ensureCacheIsFull(byteSize);
			bufferPos += byteSize;
			filePos += byteSize;
			listPos++;
		}
	} // end while (listPos < listCount)

	close(fd);

	printf("Total number of terms: %d.\n", termCount);
	printf("Average term length: %.1lf bytes.\n\n", totalTermLength * 1.0 / termCount);
} // end of processIndexFile(char*)


static int64_t getFilePointer(char *term) {
	if (strcmp(term, (char*)&termArray[arrayPointers[0]]) < 0)
		return -1;
	int lower = 0;
	int upper = groupCount - 1;
	while (upper > lower) {
		int middle = (lower + upper + 1) >> 1;
		char *middleTerm = (char*)&termArray[arrayPointers[middle]];
		int diff = strcmp(middleTerm, term);
		if (diff == 0)
			upper = lower = middle;
		else if (diff > 0)
			upper = middle - 1;
		else
			lower = middle;
	}
	TermDescriptor descriptors[1024];
	getGroup(lower, descriptors);
	for (int i = 0; i < groupSize; i++)
		if (strcmp(descriptors[i].term, term) == 0)
			return descriptors[i].filePointer;
	return -1;
} // end of getFilePointer(char*)


int main(int argc, char **argv) {
	if (argc != 5) {
		fprintf(stderr, "Usage:  get_dictionary_size INDEX_FILE COMPRESSION_METHOD INDEX_BLOCK_SIZE GROUP_SIZE > OUTPUT_FILE\n\n");
		fprintf(stderr, "Prints the size of the in-memory dictionary for an index with given block size.\n");
		fprintf(stderr, "Terms in the dictionary are combined into groups of GROUP_SIZE elements each.\n");
		fprintf(stderr, "Each group is compressed using the given method (NONE, FRONTCODING, LZW, BOTH).\n\n");
		return 1;
	}
	if (strcasecmp(argv[2], "NONE") == 0)
		compressionMethod = COMPRESSION_NONE;
	else if (strcasecmp(argv[2], "FRONTCODING") == 0)
		compressionMethod = COMPRESSION_FRONTCODING;
	else if (strcasecmp(argv[2], "LZW") == 0)
		compressionMethod = COMPRESSION_LZW;
	else if ((strcasecmp(argv[2], "FC+LZW") == 0) || (strcasecmp(argv[2], "BOTH") == 0))
		compressionMethod = COMPRESSION_BOTH;
	else {
		fprintf(stderr, "Invalid compression method. Use \"NONE\", \"FRONTCODING\", \"LZW\", or \"BOTH\".\n\n");
		exit(1);
	}
	int status;
	status = sscanf(argv[3], "%lld", &indexBlockSize);
	assert(status == 1);
	assert(indexBlockSize < 999999);
	status = sscanf(argv[4], "%d", &groupSize);
	assert(status == 1);
	assert(groupSize > 0);

	// allocate memory for term pointers and file pointers
	arrayPointers = new int32_t[50000000];
	termArray = new char[1200000000];
	
	processIndexFile(argv[1]);
	long long totalSpace = 4 * groupCount + termArraySize;
	printf("Total space consumption of dictionary: %lld bytes (%d groups)\n", totalSpace, groupCount);

	for (int i = 0; i < 100000; i++) {
		randomTerms[i] = new char[32];
		getRandomTerm(randomTerms[i]);
	}

	int start = currentTimeMillis(), end = start + 1;
	double searchOperations = 0;
	do {
		start = currentTimeMillis(), end = start + 1;
		searchOperations = 0;
		do {
			for (int i = 0; i < 100000; i++)
				int64_t filePointer = getFilePointer(randomTerms[i]);
			searchOperations += 100000;
		}	while ((end = currentTimeMillis()) < start + 10000);
	} while (end < start);

	printf("Lookup performance: %.2lf ns per term\n", (end - start) / searchOperations * 1E6);
	return 0;
}


