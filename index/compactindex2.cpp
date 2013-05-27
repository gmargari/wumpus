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
 * Implementation of the CompactIndex2 class.
 *
 * author: Stefan Buettcher
 * created: 2007-07-13
 * changed: 2008-08-14
 **/


#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include "compactindex2.h"
#include "segmentedpostinglist.h"
#include "../misc/all.h"
#include "../stemming/stemmer.h"


static const char *LOG_ID = "CompactIndex2";

static const byte CI2_GUARDIAN[4] = { 255, 255, 255, 0 };


/**
 * Whenever we see a term whose postings list consumes more than this
 * many bytes, we force it into a separate index block, only containing
 * this one term. That index block will have a slightly different format,
 * allowing us to store the list of synchronization points
 * (PostingListSegmentHeader) *after* the postings data instead of before.
 **/
static const int SEPARATE_INDEX_BLOCK_THRESHOLD = 4 * BYTES_PER_INDEX_BLOCK;


bool CompactIndex2::canRead(const char *fileName) {
	FILE *f = fopen(fileName, "r");
	if (f == NULL)
		return false;
	char fileStart[256];
	int status = fread(fileStart, 1, CI2_SIGNATURE_LENGTH, f);
	fclose(f);
	if (status != CI2_SIGNATURE_LENGTH)
		return false;
	return (memcmp(fileStart, CI2_SIGNATURE, CI2_SIGNATURE_LENGTH) == 0);
} // end of canRead(char*)


CompactIndex2::CompactIndex2(Index *owner, const char *fileName, bool create, bool use_O_DIRECT) {
	this->owner = owner;
	this->fileName = duplicateString(fileName);
	this->compressor = compressorForID[indexCompressionMode];
	this->use_O_DIRECT = use_O_DIRECT;
	baseFile = NULL;
	inMemoryIndex = NULL;

	if (!create)
		initializeForQuerying();
	else {
		header.termCount = 0;
		header.listCount = 0;
		header.postingCount = 0;
		header.descriptorCount = 0;

		int flags = O_CREAT | O_TRUNC | O_RDWR | O_LARGEFILE;
		if (use_O_DIRECT)
			flags |= (O_DIRECT | O_SYNC);
		fileHandle = open(fileName, flags, DEFAULT_FILE_PERMISSIONS);

		if (fileHandle < 0) {
			snprintf(errorMessage, sizeof(errorMessage),
					"Unable to create on-disk index: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			perror(NULL);
			exit(1);
		}
		else {
			// create File object to be used by all posting lists; initial usage count: 1
			// setting the usage count to 1 makes sure the object is not destroyed by
			// its children (see FileFile for details)
			baseFile = new FileFile(fileName, (off_t)0, 1);
		}

		// allocate space for write buffer; must be properly mem-aligned because we
		// want to be able to access the output file with O_DIRECT
		int status = posix_memalign((void**)&writeCache, 4096, WRITE_CACHE_SIZE);
		if (status != 0) {
			log(LOG_ERROR, LOG_ID, "Unable to allocate aligned memory for write buffer");
			perror("posix_memalign");
			exit(1);
		}

		// write file signature into write cache
		memcpy(writeCache, CI2_SIGNATURE, CI2_SIGNATURE_LENGTH);
		cacheBytesUsed = CI2_SIGNATURE_LENGTH;

		// initialize cache status variables
		bytesWrittenToFile = 0;
		tempSegmentCount = 0;
		totalSizeOfTempSegments = 0;
		lastTermAdded[0] = 0;
		readOnly = false;

		// initialize descriptor table
		allocatedForDescriptors = 4096;
		usedByDescriptors = 0;
		compressedDescriptors = (byte*)malloc(allocatedForDescriptors);
		groupDescriptors = NULL;
		firstTermInLastBlock[0] = 0;
		startPosOfLastBlock = 0;
		addDescriptor("");

		usedByPLSH = 0;
		allocatedForPLSH = 256;
		temporaryPLSH = (byte*)malloc(allocatedForPLSH);

		currentTermLastPosting = 0;
		currentTermSegmentCount = 0;
		currentTermMarker = -1;

		// print useful debug message and seek to start of file
		sprintf(errorMessage, "Creating new on-disk index: %s", fileName);
		log(LOG_DEBUG, LOG_ID, errorMessage);
		if (!use_O_DIRECT)
			forced_write(fileHandle, &header, sizeof(header));
		lseek(fileHandle, 0, SEEK_SET);
	}
} // end of CompactIndex2(Index*, char*, bool, bool)


CompactIndex2::CompactIndex2(Index *owner, const char *fileName) {
	this->owner = owner;
	this->fileName = duplicateString(fileName);
	this->compressor = compressorForID[indexCompressionMode];
	this->use_O_DIRECT = use_O_DIRECT;
	baseFile = NULL;
	inMemoryIndex = NULL;

	initializeForQuerying();
	loadIndexIntoMemory();
} // end of CompactIndex2(Index*, char*)


void CompactIndex2::initializeForQuerying() {
	readOnly = true;

	fileHandle = open(fileName, O_RDONLY | O_LARGEFILE);
	if (fileHandle < 0) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to open on-disk index: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		perror(NULL);
		exit(1);
	}
	else {
		// create File object to be used by all posting lists; initial usage count: 1
		// setting the usage count to 1 makes sure the object is not destroyed by
		// its children (see FileFile for details)
		baseFile = new FileFile(fileName, (off_t)0, 1);
	}

	// read header from end of file
	readRawData(getByteSize() - sizeof(header), &header, sizeof(header));

	// read compressed descriptor sequence
	usedByDescriptors = allocatedForDescriptors = header.compressedDescriptorSize;
	endOfPostingsData = getByteSize() - sizeof(header) - usedByDescriptors;
	compressedDescriptors = (byte*)malloc(usedByDescriptors);
	readRawData(endOfPostingsData, compressedDescriptors, usedByDescriptors);

	sprintf(errorMessage, "On-disk index loaded: %s", fileName);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage, "  terms: %lld, segments: %lld, postings: %lld, descriptors: %lld (%d bytes)",
			(long long)header.termCount,
			(long long)header.listCount,
			(long long)header.postingCount,
			(long long)header.descriptorCount,
			usedByDescriptors);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	// build search array from compressed descriptor sequence
	dictionaryGroupCount =
		(header.descriptorCount + DICTIONARY_GROUP_SIZE - 1) / DICTIONARY_GROUP_SIZE;
	groupDescriptors =
		typed_malloc(CompactIndex2_DictionaryGroup, dictionaryGroupCount + 1);
	int64_t filePos = 0;
	char prevTerm[MAX_TOKEN_LENGTH * 2] = { 0 };
	uint32_t inPos = 0;
	for (int i = 0; i < dictionaryGroupCount; i++) {
		assert(inPos < 2000000000);
		offset delta;

		groupDescriptors[i].groupStart = inPos;
		inPos += decodeFrontCoding(&compressedDescriptors[inPos], prevTerm, groupDescriptors[i].groupLeader);
		inPos += decodeVByteOffset(&delta, &compressedDescriptors[inPos]);
		filePos += delta;
		groupDescriptors[i].filePosition = filePos;
		strcpy(prevTerm, groupDescriptors[i].groupLeader);

		for (int k = 1; (k < DICTIONARY_GROUP_SIZE) && (inPos < usedByDescriptors); k++) {
			char term[MAX_TOKEN_LENGTH * 2];
			inPos += decodeFrontCoding(&compressedDescriptors[inPos], prevTerm, term);
			inPos += decodeVByteOffset(&delta, &compressedDescriptors[inPos]);
			strcpy(prevTerm, term);
			filePos += delta;
		}
	} // end for (int i = 0; i < dictionaryGroupCount; i++)

	temporaryPLSH = NULL;

} // end of initializeForQuerying()


CompactIndex2::~CompactIndex2() {
	if (fileHandle < 0)
		return;

	if (!readOnly) {
		if (use_O_DIRECT) {
			// if we access the output file directly, we need to close the file handle
			// now and re-acquire a new one, because the write operations in the destructor
			// are not properly mem-aligned
			close(fileHandle);
			fileHandle = open(fileName, O_RDWR | O_LARGEFILE, DEFAULT_FILE_PERMISSIONS);
			if (fileHandle < 0) {
				log(LOG_ERROR, LOG_ID, "Unable to re-open target file.");
				perror("~CompactIndex2");
				exit(1);
			}
		}
		flushWriteCache();

		// write descriptors
		lseek(fileHandle, bytesWrittenToFile, SEEK_SET);
		bytesWrittenToFile +=
			writeRawData(bytesWrittenToFile, compressedDescriptors, usedByDescriptors);

		// write header data
		header.compressedDescriptorSize = usedByDescriptors;
		bytesWrittenToFile +=
			writeRawData(bytesWrittenToFile, &header, sizeof(header));
		forced_ftruncate(fileHandle, lseek(fileHandle, 0, SEEK_CUR));
		realFree(writeCache);
		writeCache = NULL;
		fsync(fileHandle);
	} // end if (!readOnly)

	sprintf(errorMessage, "Freeing memory for on-disk index: %s", fileName);
	long long pc = header.postingCount;
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage,
			"  terms: %lld, segments: %lld, postings: %lld, descriptors: %lld (%d bytes)",
			static_cast<long long>(header.termCount),
			static_cast<long long>(header.listCount),
			static_cast<long long>(header.postingCount),
			static_cast<long long>(header.descriptorCount),
			usedByDescriptors);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	FREE_AND_SET_TO_NULL(inMemoryIndex);
	FREE_AND_SET_TO_NULL(temporaryPLSH);
	FREE_AND_SET_TO_NULL(compressedDescriptors);
	FREE_AND_SET_TO_NULL(groupDescriptors);
	FREE_AND_SET_TO_NULL(fileName);
	if (baseFile != NULL) {
		delete baseFile;
		baseFile = NULL;
	}

	close(fileHandle);
	fileHandle = -1;
} // end of ~CompactIndex2()


void CompactIndex2::flushWriteCache() {
	if (readOnly)
		return;
	LocalLock lock();
	copySegmentsToWriteCache();
	lseek(fileHandle, (off_t)bytesWrittenToFile, SEEK_SET);
	forced_write(fileHandle, writeCache, cacheBytesUsed);
	bytesWrittenToFile += cacheBytesUsed;
	cacheBytesUsed = 0;
	addDescriptor((char*)CI2_GUARDIAN);
} // end of flushWriteCache()


void CompactIndex2::updateMarker() {
	int64_t markerValue =
		bytesWrittenToFile + cacheBytesUsed - currentTermMarker - sizeof(markerValue);
	if (currentTermMarker >= bytesWrittenToFile) {
		// if the marker is still in the write cache, then things are easy
		memcpy(&writeCache[currentTermMarker - bytesWrittenToFile], &markerValue, sizeof(markerValue));
	}
	else {
		// otherwise, we have to write its new value to the file
		int fd = open(fileName, O_RDWR);
		if (fd < 0) {
			log(LOG_ERROR, LOG_ID, "Unable to adjust segment marker.");
			perror("open");
			exit(1);
		}
		lseek(fd, currentTermMarker, SEEK_SET);
		forced_write(fd, &markerValue, sizeof(markerValue));
		close(fd);

		// what may happen, however, is that the marker is still partially in
		// memory; in that case, we have to update the in-memory part as well
		if (currentTermMarker + sizeof(markerValue) > bytesWrittenToFile) {
			byte temp[sizeof(markerValue)];
			memcpy(temp, &markerValue, sizeof(markerValue));
			int overlap = currentTermMarker + sizeof(markerValue) - bytesWrittenToFile;
			assert(overlap < sizeof(markerValue));
			memcpy(writeCache, &temp[sizeof(markerValue) - overlap], overlap);
		}
	}
} // end of updateMarker()


void CompactIndex2::copySegmentsToWriteCache() {
	if (currentTermSegmentCount <= 0)
		return;

	writeCache[cacheBytesUsed++] = 0;

	if (currentTermMarker < 0)
		return;

	updateMarker();
	currentTermMarker = -1;

	if (cacheBytesUsed + 16 > WRITE_CACHE_SIZE)
		flushPartialWriteCache();
	cacheBytesUsed +=
		encodeVByte32(currentTermSegmentCount, &writeCache[cacheBytesUsed]);
	cacheBytesUsed +=
		encodeVByte32(usedByPLSH, &writeCache[cacheBytesUsed]);

	int pos = 0;
	while (pos < usedByPLSH) {
		int chunkSize = MIN(65536, usedByPLSH - pos);
		if (cacheBytesUsed + chunkSize > WRITE_CACHE_SIZE)
			flushPartialWriteCache();
		memcpy(&writeCache[cacheBytesUsed], &temporaryPLSH[pos], chunkSize);
		pos += chunkSize;
		cacheBytesUsed += chunkSize;
	}

	// reset status variables for current term's segment headers
	currentTermSegmentCount = 0;
	usedByPLSH = 0;
} // end of copySegmentsToWriteCache()


void CompactIndex2::addDescriptor(const char *term) {
	// make sure we have enough space for the incoming descriptor
	if (usedByDescriptors + MAX_TOKEN_LENGTH + 32 > allocatedForDescriptors) {
		allocatedForDescriptors =
			(int)(allocatedForDescriptors * DESCRIPTOR_GROWTH_RATE + 4096);
		compressedDescriptors =
			(byte*)realloc(compressedDescriptors, allocatedForDescriptors);
	}

	// add compressed descriptor
	usedByDescriptors +=
		encodeFrontCoding(term, firstTermInLastBlock, &compressedDescriptors[usedByDescriptors]);
	int64_t filePos = bytesWrittenToFile + cacheBytesUsed;
	usedByDescriptors +=
		encodeVByteOffset(filePos - startPosOfLastBlock, &compressedDescriptors[usedByDescriptors]);
	assert(usedByDescriptors <= allocatedForDescriptors);

	// update the reference string used in front-coding
	strcpy(firstTermInLastBlock, term);
	startPosOfLastBlock = filePos;
	header.descriptorCount++;
} // end of addDescriptor(char*)


void CompactIndex2::addPostings(const char *term, byte *postings,
		int byteLength, int count, offset first, offset last) {
	assert(!readOnly);
	assert((count > 0) && (last >= first) && (term[0] != 0));

	// if we receive more postings than we can put into a list segment without
	// violating the MIN_SEGMENT_SIZE/MAX_SEGMENT_SIZE constraint, we need to
	// split the list into sub-lists of manageable size: decompress and pass
	// to the method that deals with uncompressed lists
	if ((count > MAX_SEGMENT_SIZE) ||
	    (extractCompressionModeFromList(postings) != indexCompressionMode)) {
		int listLengthFromCompressor;
		offset *uncompressed =
			decompressList(postings, byteLength, &listLengthFromCompressor, NULL);
		assert(listLengthFromCompressor == count);
		CompactIndex::addPostings(term, uncompressed, count);
		free(uncompressed);
		return;
	}

	// check if the terms come in pre-sorted
	int comparison = strcmp(term, lastTermAdded);
	assert(comparison >= 0);
	if (comparison != 0) {
		// new term: need to copy segments descriptors for old one to write cache
		copySegmentsToWriteCache();
		if (cacheBytesUsed + 256 > WRITE_CACHE_SIZE)
			flushPartialWriteCache();
		usedByPLSH = 0;
		currentTermLastPosting = 0;
		currentTermSegmentCount = 0;

		// sorry; we do not allow any term that is right of the guardian term
		if (strcmp(term, (char*)CI2_GUARDIAN) >= 0)
			return;

		if (bytesWrittenToFile + cacheBytesUsed >= startPosOfLastBlock + BYTES_PER_INDEX_BLOCK)
			addDescriptor(term);

		cacheBytesUsed +=
			encodeFrontCoding(term, lastTermAdded, &writeCache[cacheBytesUsed]);
		strcpy(lastTermAdded, term);

		header.termCount++;
	}

	// add current list segment to segments accumulated for current term
	if (usedByPLSH + 256 > allocatedForPLSH) {
		allocatedForPLSH = (int)(allocatedForPLSH * 1.21 + 4096);
		temporaryPLSH = (byte*)realloc(temporaryPLSH, allocatedForPLSH);
	}
	PostingListSegmentHeader plsh;
	plsh.postingCount = count;
	plsh.byteLength = byteLength;
	plsh.firstElement = first;
	plsh.lastElement = last;
	usedByPLSH += compressPLSH(&plsh, currentTermLastPosting, &temporaryPLSH[usedByPLSH]);

	if (cacheBytesUsed + byteLength + 256 > WRITE_CACHE_SIZE)
		flushPartialWriteCache();

	if (currentTermSegmentCount > 0) {
		// send continuation flag for current list
		writeCache[cacheBytesUsed++] = 255;

		if (currentTermSegmentCount == 1) {
			// if we are at the second segment for the current term, then reserve space
			// for a 64-bit marker; the marker's value will be set later on and can be
			// used by the query processor to seek directly to the term's list of sync
			// points, skipping over the postings data when initializing the list object
			currentTermMarker = bytesWrittenToFile + cacheBytesUsed;
			int64_t dummyMarker;
			cacheBytesUsed += sizeof(dummyMarker);
		}
	} // end if (currentTermSegmentCount > 0)

	cacheBytesUsed += compressPLSH(&plsh, currentTermLastPosting, &writeCache[cacheBytesUsed]);
	memcpy(&writeCache[cacheBytesUsed], postings, byteLength);
	cacheBytesUsed += byteLength;
	
	currentTermLastPosting = last;
	currentTermSegmentCount++;

	header.postingCount += count;
	header.listCount++;
} // end of addPostings(char*, byte*, int, int, offset, offset)


int64_t CompactIndex2::getBlockStart(const char *term, char *blockLeader) {
	if (strcmp(term, groupDescriptors[0].groupLeader) < 0)
		return -1;
	if (strcmp(term, (char*)CI2_GUARDIAN) >= 0)
		return -1;

	// do a binary search for the dictionary group that might contain the term
	int lower = 0, upper = dictionaryGroupCount - 1;
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		if (strcmp(term, groupDescriptors[middle].groupLeader) < 0)
			upper = middle - 1;
		else
			lower = middle;
	}

	int pos = groupDescriptors[lower].groupStart;
	int groupEnd = usedByDescriptors;
	if (lower < dictionaryGroupCount - 1)
		groupEnd = groupDescriptors[lower + 1].groupStart;

	// perform a sequential scan of the current group, identifying the
	// index block that may contain the given term
	offset delta;
	char prevTerm[MAX_TOKEN_LENGTH * 2], t[MAX_TOKEN_LENGTH * 2];
	strcpy(prevTerm, groupDescriptors[lower].groupLeader);
	int64_t filePosition = groupDescriptors[lower].filePosition;
	pos += decodeFrontCoding(&compressedDescriptors[pos], prevTerm, t);
	pos += decodeVByteOffset(&delta, &compressedDescriptors[pos]);

	while (pos != groupEnd) {
		pos += decodeFrontCoding(&compressedDescriptors[pos], prevTerm, t);
		pos += decodeVByteOffset(&delta, &compressedDescriptors[pos]);
		if (strcmp(term, t) < 0)
			break;
		strcpy(prevTerm, t);
		filePosition += delta;
	}

	strcpy(blockLeader, prevTerm);
	return filePosition;
} // end of getBlockStart(char*, char*)


ExtentList * CompactIndex2::getPostings2(const char *term) {
	if ((header.descriptorCount <= 0) || (header.termCount <= 0))
		return new ExtentList_Empty();

	// obtain the file position of the block containing the term
	char prevTerm[MAX_TOKEN_LENGTH * 2], t[MAX_TOKEN_LENGTH * 2];
	int64_t filePosition = getBlockStart(term, prevTerm);
	if (filePosition < 0)
		return new ExtentList_Empty();

	// we have identified the index block that potentially contains the
	// term that we are looking for; load first BYTES_PER_INDEX_BLOCK bytes
	// into memory and conduct another sequential scan on those data
	PostingListSegmentHeader plsh;
	byte buffer[BYTES_PER_INDEX_BLOCK + 256];
	int status = readRawData(filePosition, buffer, sizeof(buffer));
	int64_t postingsPosition = -1;
	int pos = 0;

	while (pos < MIN(status, BYTES_PER_INDEX_BLOCK)) {
		// extract term and check whether this is the one that we are looking for
		pos += decodeFrontCoding(&buffer[pos], prevTerm, t);
		strcpy(prevTerm, t);
		int comparison = strcmp(t, term);
		if (comparison >= 0) {
			if (comparison == 0)
				postingsPosition = filePosition + pos;
			break;
		}

		// skip over the postings for the current term
		int segmentsSeen = 0;
		do {
			if (++segmentsSeen == 2)
				pos += sizeof(int64_t);
			pos += decompressPLSH(&buffer[pos], 0, &plsh);
			pos += plsh.byteLength;
			if (pos >= status)
				break;
		} while (buffer[pos++] == 255);
		if ((segmentsSeen > 1) && (pos < status)) {
			int32_t segmentCount, segmentSize;
			pos += decodeVByte32(&segmentCount, &buffer[pos]);
			pos += decodeVByte32(&segmentSize, &buffer[pos]);
			assert(segmentCount == segmentsSeen);
			pos += segmentSize;
		}
	} // end while (pos < status)

	// if we were unable to find the term, return an empty list
	if (postingsPosition < 0)
		return new ExtentList_Empty();

	LocalLock lock(this);

	// load the first list segment for the term into memory ("tempBuf")
	pos += decompressPLSH(&buffer[pos], 0, &plsh);
	byte *tempBuf = &buffer[pos];
	bool mustFreeTempBuf = false;
	if (pos + plsh.byteLength + 32 > sizeof(buffer))
		tempBuf = getRawData(filePosition + pos, plsh.byteLength + 32, &mustFreeTempBuf);

	int32_t segmentCount;
	PostingListSegmentHeader *segmentHeaders;
	int64_t *segmentPositions;

	if (tempBuf[plsh.byteLength] == 0) {
		// this is the only list segment for the given term (continuation flag == 0);
		// build SegmentedPostingList instance directly from data found in plsh
		segmentCount = 1;
		segmentHeaders = typed_malloc(PostingListSegmentHeader, segmentCount);
		segmentPositions = typed_malloc(int64_t, segmentCount);
		segmentHeaders[0] = plsh;
		segmentPositions[0] = filePosition + pos;
	}
	else {
		// more segments to follow (continuation flag == 255); seek to beginning of
		// sync point list and build SegmentedPostingList instance from those data
		int64_t markerValue;
		memcpy(&markerValue, &tempBuf[plsh.byteLength + 1], sizeof(markerValue));
		int64_t markerFilePos = filePosition + pos + plsh.byteLength + 1;
		int64_t headerFilePos = markerFilePos + sizeof(markerValue) + markerValue;
		headerFilePos += readRawData(headerFilePos, buffer, sizeof(buffer));

		int32_t dummy32;
		pos = decodeVByte32(&segmentCount, &buffer[0]);
		pos += decodeVByte32(&dummy32, &buffer[pos]);
		segmentHeaders = typed_malloc(PostingListSegmentHeader, segmentCount);
		segmentPositions = typed_malloc(int64_t, segmentCount);

		offset referencePosting = 0;
		for (int i = 0; i < segmentCount; i++) {
			if (pos > sizeof(buffer) - 256) {
				memmove(buffer, &buffer[pos], sizeof(buffer) - pos);
				headerFilePos += readRawData(headerFilePos, &buffer[sizeof(buffer) - pos], pos);
				pos = 0;
			}
			int headerSize =
				decompressPLSH(&buffer[pos], referencePosting, &segmentHeaders[i]);
			pos += headerSize;
			postingsPosition += headerSize;
			if (i > 0) {
				postingsPosition++;
				if (i == 1)
					postingsPosition += sizeof(markerValue);
			}
			segmentPositions[i] = postingsPosition;
			referencePosting = segmentHeaders[i].lastElement;
			postingsPosition += segmentHeaders[i].byteLength;
		}
	} // end else [tempBuf[plsh.byteLength] != 0]
	if (mustFreeTempBuf)
		free(tempBuf);

#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
	SPL_InMemorySegment *splSegments = typed_malloc(SPL_InMemorySegment, segmentCount);
	FileFile *file = NULL;
#else
	SPL_OnDiskSegment *splSegments = typed_malloc(SPL_OnDiskSegment, segmentCount);
	FileFile *file = getFile();
#endif
	for (int i = 0; i < segmentCount; i++) {
		splSegments[i].count = segmentHeaders[i].postingCount;
		splSegments[i].byteLength = segmentHeaders[i].byteLength;
		splSegments[i].firstPosting = segmentHeaders[i].firstElement;
		splSegments[i].lastPosting = segmentHeaders[i].lastElement;
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
		splSegments[i].postings = (byte*)malloc(segmentHeaders[i].byteLength);
		readRawData(segmentPositions[i], splSegments[i].postings, segmentHeaders[i].byteLength);
#else
		splSegments[i].file = new FileFile(file, segmentPositions[i]);
#endif
	}

	if ((segmentCount == 0) && (file != NULL))
		delete file;
	free(segmentHeaders);
	free(segmentPositions);

#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
	return new SegmentedPostingList(splSegments, segmentCount, true);
#else
	return new SegmentedPostingList(splSegments, segmentCount);
#endif
} // end of getPostings2(char*)


ExtentList * CompactIndex2::getPostingsForWildcardQuery(const char *pattern, const char *stem) {
	if ((header.descriptorCount <= 0) || (header.termCount <= 0))
		return new ExtentList_Empty();

	char *prefix = duplicateString(pattern);
	for (int i = 0; prefix[i] != 0; i++)
		if (IS_WILDCARD_CHAR(prefix[i])) {
			prefix[i] = 0;
			break;
		}
	bool isDocumentLevel = startsWith(prefix, "<!>");
	int prefixLen = strlen(prefix);
	if (prefixLen < (isDocumentLevel ? 5 : 2)) {
		free(prefix);
		return new ExtentList_Empty();
	}
	
	char t[MAX_TOKEN_LENGTH * 2], prevTerm[MAX_TOKEN_LENGTH * 2];
	int64_t filePosition = getBlockStart(prefix, prevTerm);
	if (filePosition < 0)
		filePosition = groupDescriptors[0].filePosition;

	// we have identified the index block that potentially contains the
	// term that we are looking for; load first BYTES_PER_INDEX_BLOCK bytes
	// into memory and conduct another sequential scan on those data
	PostingListSegmentHeader plsh;
	byte buffer[BYTES_PER_INDEX_BLOCK + 256];
	int status = readRawData(filePosition, buffer, sizeof(buffer));
	int pos = 0;

	LocalLock lock(this);

	int termsFound = 0, termsAllocated = 256;
	ExtentList **lists = typed_malloc(ExtentList*, termsAllocated);

#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
	SPL_InMemorySegment *splSegments = NULL;
	FileFile *file = NULL;
#else
	SPL_OnDiskSegment *splSegments = NULL;
	FileFile *file = getFile();
#endif

	pos += decodeFrontCoding(&buffer[pos], prevTerm, t);
	strcpy(prevTerm, t);
	while (strncmp(prevTerm, prefix, prefixLen) <= 0) {
		int64_t postingsPosition = filePosition + pos;
		int comparison = strncmp(prevTerm, prefix, prefixLen);
		PostingListSegmentHeader plsh;

		// make sure the current term matches the prefix query and also satisfies the
		// stemming criterion
		if (comparison == 0)
			if (fnmatch(pattern, prevTerm, 0) != 0)
				comparison = -1;
		if ((comparison == 0) && (stem != NULL)) {
			char tempForStemming[MAX_TOKEN_LENGTH * 2];
			strcpy(tempForStemming, prevTerm);
			if (isDocumentLevel)
				Stemmer::stemEnglish(&tempForStemming[3]);
			else
				Stemmer::stemEnglish(tempForStemming);
			if (strcmp(tempForStemming, stem) != 0)
				comparison = -1;
		} // end if ((comparison == 0) && (stem != NULL))

		int segmentsSeen = 0;
		do {
			if (++segmentsSeen == 2) {
				int64_t markerValue;
				memcpy(&markerValue, &buffer[pos], sizeof(markerValue));
				postingsPosition += sizeof(markerValue);
				filePosition += pos + sizeof(markerValue) + markerValue;
				status = readRawData(filePosition, buffer, sizeof(buffer));
				pos = 0;

				int32_t segmentCount, segmentSize;
				pos += decodeVByte32(&segmentCount, &buffer[pos]);
				pos += decodeVByte32(&segmentSize, &buffer[pos]);

				if (comparison == 0) {
					byte *compressedHeaders = (byte*)malloc(segmentSize);
					readRawData(filePosition + pos, compressedHeaders, segmentSize);
					int inPos = decompressPLSH(&compressedHeaders[0], 0, &plsh);
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
					splSegments = typed_realloc(SPL_InMemorySegment, splSegments, segmentCount);
#else
					splSegments = typed_realloc(SPL_OnDiskSegment, splSegments, segmentCount);
#endif
					for (int i = 1; i < segmentCount; i++) {
						int headerSize = decompressPLSH(&compressedHeaders[inPos], plsh.lastElement, &plsh);
						inPos += headerSize;
						postingsPosition += headerSize;
						splSegments[i].count = plsh.postingCount;
						splSegments[i].byteLength = plsh.byteLength;
						splSegments[i].firstPosting = plsh.firstElement;
						splSegments[i].lastPosting = plsh.lastElement;
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
						splSegments[i].postings = (byte*)malloc(plsh.byteLength);
						readRawData(postingsPosition, splSegments[i].postings, plsh.byteLength);
#else
						splSegments[i].file = new FileFile(file, postingsPosition);
#endif
						postingsPosition += plsh.byteLength + 1;
					}

					free(compressedHeaders);
					segmentsSeen = segmentCount;
				} // end if (comparison == 0)

				pos += segmentSize;
				break;
			} // end if (++segmentsSeen == 2)

			int headerSize = decompressPLSH(&buffer[pos], 0, &plsh);
			pos += headerSize;
			postingsPosition += headerSize;

			// if the current term matches the query, collect postings data
			if (comparison == 0) {
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
				splSegments = typed_malloc(SPL_InMemorySegment, 1);
				splSegments[0].postings = (byte*)malloc(plsh.byteLength);
				readRawData(postingsPosition, splSegments[0].postings, plsh.byteLength);
#else
				splSegments = typed_malloc(SPL_OnDiskSegment, 1);
				splSegments[0].file = new FileFile(file, postingsPosition);
#endif
				splSegments[0].count = plsh.postingCount;
				splSegments[0].byteLength = plsh.byteLength;
				splSegments[0].firstPosting = plsh.firstElement;
				splSegments[0].lastPosting = plsh.lastElement;
			} // end if (comparison == 0)

			pos += plsh.byteLength;
			postingsPosition += plsh.byteLength + 1;

			if (pos + 256 > status) {
				filePosition += pos;
				status = readRawData(filePosition, buffer, sizeof(buffer));
				pos = 0;
			}
		} while (buffer[pos++] == 255);

		// add current list to set of lists to return to caller
		if ((comparison == 0) && (segmentsSeen > 0)) {
			if (termsFound >= termsAllocated)
				lists = typed_realloc(ExtentList*, lists, termsAllocated = (termsAllocated * 2));
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
			lists[termsFound++] = new SegmentedPostingList(splSegments, segmentsSeen, true);
#else
			lists[termsFound++] = new SegmentedPostingList(splSegments, segmentsSeen);
#endif
		}

		if (pos + 256 > status) {
			// refill buffer if necessary
			filePosition += pos;
			status = readRawData(filePosition, buffer, sizeof(buffer));
			pos = 0;
		}

		// fetch next term from buffer
		pos += decodeFrontCoding(&buffer[pos], prevTerm, t);
		strcpy(prevTerm, t);
	} // end while (strncmp(prevTerm, prefix, prefixLen) <= 0)

	free(prefix);

	if (termsFound == 0) {
		if (file != NULL)
			delete file;
		free(lists);
		return new ExtentList_Empty();
	}
	else if (termsFound == 1) {
		ExtentList *result = lists[0];
		free(lists);
		return result;
	}
	else if (isDocumentLevel)
		return ExtentList::mergeDocumentLevelLists(lists, termsFound);
	else
		return new ExtentList_OR_Postings(lists, termsFound);
} // end of getPostingsForWildcardQuery(char*, char*)


int64_t CompactIndex2::getTermCount() {
	return header.termCount;
}


int64_t CompactIndex2::getPostingCount() {
	return header.postingCount;
}


void CompactIndex2::getClassName(char *target) {
	strcpy(target, LOG_ID);
} // end of getClassName(char*)


