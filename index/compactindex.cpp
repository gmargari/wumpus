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
 * Implementation of the CompactIndex class.
 *
 * author: Stefan Buettcher
 * created: 2005-01-07
 * changed: 2008-08-14
 **/


#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "compactindex.h"
#include "compactindex2.h"
#include "index.h"
#include "index_iterator2.h"
#include "postinglist.h"
#include "segmentedpostinglist.h"
#include "../misc/all.h"
#include "../stemming/stemmer.h"


static const char * LOG_ID = "CompactIndex";

static const byte CI_GUARDIAN[4] = { 255, 255, 255, 0 };


// Factory methods, able to distinguish between CompactIndex and CompactIndex2.

CompactIndex * CompactIndex::getIndex(
		Index *owner, const char *fileName, bool create, bool use_O_DIRECT) {
	bool isNewIndexFormat = false;
	bool loadIntoRAM;
	getConfigurationBool("ALL_INDICES_IN_MEMORY", &loadIntoRAM, false);
	if (loadIntoRAM) {
		assert(owner != NULL);
		assert(!create);
		if (!owner->readOnly) {
			log(LOG_ERROR, LOG_ID, "ALL_INDICES_IN_MEMORY incompatible with non-read-only index.");
			log(LOG_ERROR, LOG_ID, "Re-start with READ_ONLY=true.");
			exit(1);
		}
		if (CompactIndex2::canRead(fileName))
			return new CompactIndex2(owner, fileName);
		else
			return new CompactIndex(owner, fileName);
	}
	else if ((create) && (USE_COMPACTINDEX_2))
		return new CompactIndex2(owner, fileName, create, use_O_DIRECT);
	else if ((!create) && (CompactIndex2::canRead(fileName)))
		return new CompactIndex2(owner, fileName, create, use_O_DIRECT);
	else
		return new CompactIndex(owner, fileName, create, use_O_DIRECT);
} // end of getIndex(Index*, char*, bool, bool)


IndexIterator * CompactIndex::getIterator(const char *fileName, int bufferSize) {
	if (CompactIndex2::canRead(fileName))
		return new IndexIterator2(fileName, bufferSize);
	else
		return new IndexIterator(fileName, bufferSize);
} // end of getIterator(char*, int)


CompactIndex::CompactIndex() {
	indexCompressionMode = INDEX_COMPRESSION_MODE;
	owner = NULL;
	fileName = NULL;
	fileHandle = -1;
	descriptors = NULL;
	inMemoryIndex = NULL;
	totalSize = 0;
} // end of CompactIndex()


CompactIndex::CompactIndex(Index *owner, const char *fileName, bool create, bool use_O_DIRECT) {
	this->owner = owner;
	this->fileName = duplicateString(fileName);
	this->compressor = compressorForID[indexCompressionMode];
	this->use_O_DIRECT = use_O_DIRECT;
	baseFile = NULL;
	inMemoryIndex = NULL;
	totalSize = 0;

	if (!create)
		initializeForQuerying();
	else {
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

		cacheBytesUsed = 0;
		bytesWrittenToFile = 0;
		tempSegmentCount = 0;
		totalSizeOfTempSegments = 0;
		lastTermAdded[0] = 0;
		readOnly = false;

		header.termCount = 0;
		header.listCount = 0;
		header.descriptorCount = 0;
		header.postingCount = 0;
		descriptorSlotCount = 256;
		descriptors = typed_malloc(CompactIndex_BlockDescriptor, descriptorSlotCount);
		addDescriptor("");

		// allocate space for write buffer; must be properly mem-aligned because we
		// want to be able to access the output file with O_DIRECT
		int status = posix_memalign((void**)&writeCache, 4096, WRITE_CACHE_SIZE);
		if (status != 0) {
			log(LOG_ERROR, LOG_ID, "Unable to allocate aligned memory for write buffer");
			perror("posix_memalign");
			exit(1);
		}

		sprintf(errorMessage, "Creating new on-disk index: %s", fileName);
		log(LOG_DEBUG, LOG_ID, errorMessage);
		if (!use_O_DIRECT)
			forced_write(fileHandle, &header, sizeof(header));
		lseek(fileHandle, 0, SEEK_SET);
	}
} // end of CompactIndex(Index*, char*, bool, bool)


CompactIndex::CompactIndex(Index *owner, const char *fileName) {
	this->owner = owner;
	this->fileName = duplicateString(fileName);
	this->compressor = compressorForID[indexCompressionMode];
	this->use_O_DIRECT = use_O_DIRECT;
	baseFile = NULL;
	inMemoryIndex = NULL;

	initializeForQuerying();
	loadIndexIntoMemory();
} // end of CompactIndex(Index*, char*)


void CompactIndex::initializeForQuerying() {
	readOnly = true;

	fileHandle = open(fileName, O_RDONLY | O_LARGEFILE);
	if (fileHandle < 0) {
		snprintf(errorMessage, sizeof(errorMessage), "Unable to open on-disk index: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
		perror(NULL);
		exit(1);
	}


	// create File object to be used by all posting lists; initial usage count: 1
	// setting the usage count to 1 makes sure the object is not destroyed by
	// its children (see FileFile for details)
	baseFile = new FileFile(fileName, (off_t)0, 1);
	readRawData(getByteSize() - sizeof(header), &header, sizeof(header));

	long long descSize =
		header.descriptorCount * sizeof(CompactIndex_BlockDescriptor);
	descriptorSlotCount = header.descriptorCount;
	descriptors = typed_malloc(CompactIndex_BlockDescriptor, descriptorSlotCount + 1);
	readRawData(getByteSize() - sizeof(header) - descSize, descriptors, descSize);

	long long pc = header.postingCount;
	sprintf(errorMessage, "On-disk index loaded: %s", fileName);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage, "  terms: %d, segments: %d, postings: %lld, descriptors: %d (%lld bytes)",
			header.termCount, header.listCount, pc, header.descriptorCount, descSize);
	log(LOG_DEBUG, LOG_ID, errorMessage);
} // end of initializeForQuerying()


void CompactIndex::loadIndexIntoMemory() {
	// load the entire index into RAM
	totalSize = getByteSize();
	inMemoryIndex = (char*)malloc(totalSize);
	lseek(fileHandle, (off_t)0, SEEK_SET);
	int64_t done = 0;
	int64_t toDo = totalSize;
	static const int BUFFER_SIZE = 256 * 1024;
	while (toDo > 0) {
		if (toDo < BUFFER_SIZE)
			toDo -= forced_read(fileHandle, &inMemoryIndex[done], toDo);
		else {
			int result = forced_read(fileHandle, &inMemoryIndex[done], BUFFER_SIZE);
			assert(result == BUFFER_SIZE);
			done += result;
			toDo -= result;
		}
	}
} // end of loadIndexIntoMemory()


CompactIndex::~CompactIndex() {
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
				perror("~CompactIndex");
				exit(1);
			}
		}
		flushWriteCache();

		// write descriptors
		long long totalDescriptorSize =
			header.descriptorCount * sizeof(CompactIndex_BlockDescriptor);
		lseek(fileHandle, bytesWrittenToFile, SEEK_SET);
		writeRawData(bytesWrittenToFile, descriptors, totalDescriptorSize);

		// write header data
		writeRawData(bytesWrittenToFile + totalDescriptorSize, &header, sizeof(header));
		forced_ftruncate(fileHandle, lseek(fileHandle, 0, SEEK_CUR));
		realFree(writeCache);
		writeCache = NULL;
		fsync(fileHandle);
	}

	sprintf(errorMessage, "Freeing memory for %s index: %s",
			(inMemoryIndex == NULL ? "on-disk" : "in-memory"), fileName);
	long long pc = header.postingCount;
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage, "  termCount = %d, listCount = %d, descriptorCount = %d, postingCount = %lld",
			header.termCount, header.listCount, header.descriptorCount, pc);
	log(LOG_DEBUG, LOG_ID, errorMessage);

	FREE_AND_SET_TO_NULL(fileName);
	FREE_AND_SET_TO_NULL(descriptors);
	FREE_AND_SET_TO_NULL(inMemoryIndex);
	if (baseFile != NULL) {
		delete baseFile;
		baseFile = NULL;
	}

	close(fileHandle);
	fileHandle = -1;
} // end of ~CompactIndex()


void CompactIndex::flushWriteCache() {
	if (readOnly)
		return;
	LocalLock lock();
	copySegmentsToWriteCache();
	lseek(fileHandle, (off_t)bytesWrittenToFile, SEEK_SET);
	forced_write(fileHandle, writeCache, cacheBytesUsed);
	bytesWrittenToFile += cacheBytesUsed;
	cacheBytesUsed = 0;
	addDescriptor((char*)CI_GUARDIAN);
} // end of flushWriteCache()


void CompactIndex::flushPartialWriteCache() {
	static const int CHUNK_SIZE = 256 * 1024;
	assert(!readOnly);
	LocalLock lock();
	assert(bytesWrittenToFile % CHUNK_SIZE == 0);
	lseek(fileHandle, (off_t)bytesWrittenToFile, SEEK_SET);
	int pos = 0;
	while (pos + CHUNK_SIZE <= cacheBytesUsed) {
		forced_write(fileHandle, &writeCache[pos], CHUNK_SIZE);
		bytesWrittenToFile += CHUNK_SIZE;
		pos += CHUNK_SIZE;
	}
	cacheBytesUsed -= pos;
	memmove(writeCache, &writeCache[pos], cacheBytesUsed);
} // end of flushPartialWriteCache()


void CompactIndex::addDescriptor(const char *term) {
	// check if we have enough space for a new descriptor
	if (header.descriptorCount == descriptorSlotCount) {
		descriptorSlotCount =
			(int)(descriptorSlotCount * DESCRIPTOR_GROWTH_RATE + 4096);
		descriptors =
			typed_realloc(CompactIndex_BlockDescriptor, descriptors, descriptorSlotCount);
	}
	// add descriptor
	strcpy(descriptors[header.descriptorCount].firstTerm, term);
	descriptors[header.descriptorCount].blockStart =
		bytesWrittenToFile + cacheBytesUsed;
	descriptors[header.descriptorCount].blockEnd =
		descriptors[header.descriptorCount].blockStart;
	header.descriptorCount++;
	startPosOfLastBlock = bytesWrittenToFile + cacheBytesUsed;
} // end of addDescriptor(char*)


void CompactIndex::addPostings(const char *term, offset *postings, int count) {
	assert(!readOnly);
	assert((count > 0) && (postings[count - 1] >= postings[0]) && (term[0] != 0));

	int byteLength;
	byte *compressed;
	bool mustReleaseLock = getLock();

	while (count > MAX_SEGMENT_SIZE + TARGET_SEGMENT_SIZE) {
		compressed = compressorForID[indexCompressionMode](postings, TARGET_SEGMENT_SIZE, &byteLength);
		addPostings(term, compressed, byteLength, TARGET_SEGMENT_SIZE,
		postings[0], postings[TARGET_SEGMENT_SIZE - 1]);
		free(compressed);
		postings = &postings[TARGET_SEGMENT_SIZE];
		count -= TARGET_SEGMENT_SIZE;
	}
	if (count > MAX_SEGMENT_SIZE) {
		compressed = compressorForID[indexCompressionMode](postings, count/2, &byteLength);
		addPostings(term, compressed, byteLength, count/2, postings[0], postings[count/2-1]);
		free(compressed);
		postings = &postings[count / 2];
		count -= count / 2;
	}
	compressed = compressorForID[indexCompressionMode](postings, count, &byteLength);
	addPostings(term, compressed, byteLength, count, postings[0], postings[count - 1]);
	free(compressed);

	if (mustReleaseLock)
		releaseLock();
} // end of addPostings(char*, offset*, int)


void CompactIndex::copySegmentsToWriteCache() {
	if (tempSegmentCount <= 0)
		return;
	// copy buffered segments for previous term to write cache
	int headerSize = sizeof(PostingListSegmentHeader);
	if (cacheBytesUsed + 65536 >= WRITE_CACHE_SIZE)
		flushPartialWriteCache();
	strcpy((char*)&writeCache[cacheBytesUsed], lastTermAdded);
	cacheBytesUsed += strlen(lastTermAdded) + 1;

	// use memcpy to write "tempSegmentCount", as the buffer might not be properly aligned
	assert(sizeof(tempSegmentCount) == sizeof(int32_t));
	memcpy(&writeCache[cacheBytesUsed], &tempSegmentCount, sizeof(int32_t));
	cacheBytesUsed += sizeof(int32_t);
#if INDEX_MUST_BE_WORD_ALIGNED
	if (cacheBytesUsed & 7)
		cacheBytesUsed += 8 - (cacheBytesUsed & 7);
#endif

	memcpy(&writeCache[cacheBytesUsed], tempSegmentHeaders, tempSegmentCount * headerSize);
	cacheBytesUsed += tempSegmentCount * headerSize;
	for (int i = 0; i < tempSegmentCount; i++) {
		if (cacheBytesUsed + tempSegmentHeaders[i].byteLength >= WRITE_CACHE_SIZE)
			flushPartialWriteCache();
		memcpy(&writeCache[cacheBytesUsed], tempSegmentData[i], tempSegmentHeaders[i].byteLength);
		cacheBytesUsed += tempSegmentHeaders[i].byteLength;
		free(tempSegmentData[i]);
	}
	tempSegmentCount = 0;
	totalSizeOfTempSegments = 0;
	if (descriptors != NULL)
		descriptors[header.descriptorCount - 1].blockEnd = bytesWrittenToFile + cacheBytesUsed;
} // end of copySegmentsToWriteCache()


void CompactIndex::addPostings(const char *term, byte *postings, int byteLength,
		int count, offset first, offset last) {
	assert(!readOnly);
	assert((count > 0) && (last >= first) && (term[0] != 0));

	// sorry; we do not allow any term that is right of the guardian term
	if (strcmp(term, (char*)CI_GUARDIAN) >= 0)
		return;

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
		addPostings(term, uncompressed, count);
		free(uncompressed);
		return;
	}

	bool mustReleaseLock = getLock();

	// check if the terms come in pre-sorted
	int comparison = strcmp(term, lastTermAdded);
	assert(comparison >= 0);
	if ((comparison != 0) || (tempSegmentCount == MAX_SEGMENTS_IN_MEMORY))
		copySegmentsToWriteCache();
	strcpy(lastTermAdded, term);
#if INDEX_MUST_BE_WORD_ALIGNED
	// pad the compressed postings in order to make everything word-aligned
	if (byteLength & 7)
		byteLength += 8 - (byteLength & 7);
#endif

	tempSegmentHeaders[tempSegmentCount].postingCount = count;
	tempSegmentHeaders[tempSegmentCount].byteLength = byteLength;
	tempSegmentHeaders[tempSegmentCount].firstElement = first;
	tempSegmentHeaders[tempSegmentCount].lastElement = last;
	tempSegmentData[tempSegmentCount++] =
		(byte*)memcpy((byte*)malloc(byteLength), postings, byteLength);
	totalSizeOfTempSegments += byteLength + sizeof(PostingListSegmentHeader);

	// make sure the current index block does not get too large; if it does,
	// insert new descriptor (in-memory dictionary entry)
	long long anticipatedFilePos =
		bytesWrittenToFile + cacheBytesUsed + tempSegmentCount * sizeof(PostingListSegmentHeader) + 64;
	if (anticipatedFilePos > startPosOfLastBlock + BYTES_PER_INDEX_BLOCK)
		if (comparison != 0)
			addDescriptor(term);

	// update member variables
	header.listCount++;
	if (comparison != 0)
		header.termCount++;
	header.postingCount += count;

	if (mustReleaseLock)
		releaseLock();
} // end of addPostings(char*, byte*, int, int, offset, offset)


ExtentList * CompactIndex::getPostings(const char *term) {
	assert(readOnly);
	ExtentList *result;

	// "<!>" happens if document-level indexing has been enabled; we have to be
	// a bit careful here, because document-level postings lists have to be merged
	// in a different way than ordinary positional postings
	bool isDocumentLevel = startsWith(term, "<!>");

	int termLen = strlen(term);
	if ((strchr(term, '?') != NULL) || (strchr(term, '*') != NULL)) {
		// make sure that the caller is not combining wildcard query with stemming
		if (strchr(term, '$') != NULL)
			result = new ExtentList_Empty();
		else
			result = getPostingsForWildcardQuery(term, NULL);
	} // end if (term[termLen - 1] == '*')
	else if (term[termLen - 1] == '$') {
		if (owner == NULL) {
			// we are not part of a larger index... assume STEMMING_LEVEL >= 2 (do not
			// manipulate the search key
			result = getPostings2(term);
		}
		else if (owner->STEMMING_LEVEL >= 2) {
			// we have a fully stemmed index here; nothing to do
			result = getPostings2(term);
		}
		else if (owner->STEMMING_LEVEL == 1) {
			// in this case, we have to search for the stemmed form with and without
			// the "$" symbol
			char withoutDollarSymbol[MAX_TOKEN_LENGTH * 2];
			strcpy(withoutDollarSymbol, term);
			withoutDollarSymbol[termLen - 1] = 0;
			ExtentList *result1 = getPostings2(term);
			ExtentList *result2 = getPostings2(withoutDollarSymbol);
			if (result1->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
				delete result1;
				result = result2;
			}
			else if (result2->getType() == ExtentList::TYPE_EXTENTLIST_EMPTY) {
				delete result2;
				result = result1;
			}
			else if (isDocumentLevel) {
				ExtentList **lists = typed_malloc(ExtentList*, 2);
				lists[0] = result1;
				lists[1] = result2;
				result = ExtentList::mergeDocumentLevelLists(lists, 2);
			}
			else
				result = new ExtentList_OR_Postings(result1, result2);
		}
		else {
			// if the stemming level is 0, we do not have any stemming information,
			// so we have to get a list of all terms sharing the given stem and merge
			// them into a big ExtentList_OR
			char withoutDollarSymbol[MAX_TOKEN_LENGTH * 2];
			strcpy(withoutDollarSymbol, term);
			withoutDollarSymbol[termLen - 1] = 0;
			if (strlen(withoutDollarSymbol) < 2)
				result = getPostings2(withoutDollarSymbol);
			else {
				char prefix[MAX_TOKEN_LENGTH * 2];
				strcpy(prefix, withoutDollarSymbol);
				prefix[MAX(2, termLen - 2)] = 0;
				strcat(prefix, "*");
				result = getPostingsForWildcardQuery(prefix, withoutDollarSymbol);
			}
		}
	} // end if (term[termLen - 1] == '$')
	else {
		// trivial case: ordinary terms are handled by getPostings2
		result = getPostings2(term);
	}

	if (result->getType() == ExtentList::TYPE_EXTENTLIST_OR) {
		// if the result is of type ExtentList_OR, we *have* to optimize at
		// this point because otherwise we might end up with an ExtentList_OR
		// instance containing several thousand individual ExtentList instances,
		// which is impossible to work with in an efficient way
		ExtentList_OR *orList = (ExtentList_OR*)result;
		if (orList->elemCount == 1) {
			result = orList->elem[0];
			orList->elemCount = 0;
			delete orList;
		}
		else if (isDocumentLevel) {
			// merge document-level lists into one big list representing their disjunction
			result = ExtentList::mergeDocumentLevelLists(orList->elem, orList->elemCount);
			orList->elemCount = 0;
			orList->elem = NULL;
			delete orList;
		}
		else {
			// merge as many sub-lists inside the disjunction as possible
			orList->optimize();
			if (orList->elemCount == 1) {
				result = orList->elem[0];
				orList->elemCount = 0;
				delete orList;
			}
		}
	} // end if (result->getType() == ExtentList::TYPE_EXTENTLIST_OR)
	
	return result;
} // end of getPostings(char*)


ExtentList * CompactIndex::getPostings2(const char *term) {
	if (header.termCount <= 0)
		return new ExtentList_Empty();
	if (strcmp(term, descriptors[0].firstTerm) < 0)
		return new ExtentList_Empty();
	if (strcmp(term, descriptors[header.descriptorCount - 1].firstTerm) >= 0)
		return new ExtentList_Empty();

	// do a binary search in the descriptor list to find the index block
	// in which this term might appear
	int lower = 0;
	int upper = header.descriptorCount - 1;
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		int comparison = strcmp(term, descriptors[middle].firstTerm);
		if (comparison < 0)
			upper = middle - 1;
		else
			lower = middle;
	} // end while (upper > lower)

	// if it hit the last descriptor, decrease by 1, since the last descriptor
	// only serves as a sentinel and does not refer to actual posting lists
	if (lower == header.descriptorCount - 1)
		lower--;
		
	// now we know that, if the term exists in the index, it has to be in the
	// block given by "lower"
	bool mustReleaseLock = getLock();

	int segmentsFound = 0;
	int segmentsAllocated = 64;
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
	SPL_InMemorySegment *splSegments = typed_malloc(SPL_InMemorySegment, segmentsAllocated);
	FileFile *file = NULL;
#else
	SPL_OnDiskSegment *splSegments = typed_malloc(SPL_OnDiskSegment, segmentsAllocated);
	FileFile *file = getFile();
#endif
	off_t filePosition = descriptors[lower].blockStart;
	
	byte buffer[65536];
	off_t positionOfLastRead = 0;
	while (filePosition < descriptors[lower].blockEnd) {
		int spaceNeededForHeaders = MAX_SEGMENTS_IN_MEMORY * sizeof(PostingListSegmentHeader);
		off_t bufferLimit = ((off_t)sizeof(buffer)) - spaceNeededForHeaders - 1024;
		if ((positionOfLastRead <= 0) || (filePosition - positionOfLastRead >= bufferLimit)) {
			positionOfLastRead = filePosition;
			readRawData(filePosition, buffer, sizeof(buffer));
		}
		int localBufferPos = filePosition - positionOfLastRead;
		char *token = (char*)&buffer[localBufferPos];
		localBufferPos += strlen(token) + 1;
		// use memcpy to extract "segmentCount", as we cannot be sure that the buffer is properly aligned
		int32_t segmentCount = 0;
		memcpy(&segmentCount, &buffer[localBufferPos], sizeof(int32_t));
		localBufferPos += sizeof(int32_t);
#if INDEX_MUST_BE_WORD_ALIGNED
		if (localBufferPos & 7)
			localBufferPos += 8 - (localBufferPos & 7);
#endif

		PostingListSegmentHeader *headers = (PostingListSegmentHeader*)&buffer[localBufferPos];
		localBufferPos += segmentCount * sizeof(PostingListSegmentHeader);
		filePosition = positionOfLastRead + localBufferPos;

		int comparison = strcmp(token, term);
		if (comparison > 0)
			break;
		if (comparison < 0)
			for (int i = 0; i < segmentCount; i++)
				filePosition += headers[i].byteLength;
		if (comparison == 0) {
			if (segmentsAllocated < segmentsFound + segmentCount) {
				segmentsAllocated = (segmentsFound + segmentCount) * 4 + 8;
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
				typed_realloc(SPL_InMemorySegment, splSegments, segmentsAllocated);
#else
				typed_realloc(SPL_OnDiskSegment, splSegments, segmentsAllocated);
#endif
			}
			for (int i = 0; i < segmentCount; i++) {
				assert(headers[i].firstElement <= headers[i].lastElement);
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
				splSegments[segmentsFound].postings = (byte*)malloc(headers[i].byteLength);
				readRawData(filePosition, splSegments[segmentsFound].postings, headers[i].byteLength);
#else
				splSegments[segmentsFound].file = new FileFile(file, filePosition);
#endif
				splSegments[segmentsFound].count = headers[i].postingCount;
				splSegments[segmentsFound].byteLength = headers[i].byteLength;
				splSegments[segmentsFound].firstPosting = headers[i].firstElement;
				splSegments[segmentsFound].lastPosting = headers[i].lastElement;
				segmentsFound++;
				filePosition += headers[i].byteLength;
			}
		}
	} // end while (filePosition < descriptors[lower].blockEnd)

	ExtentList *result = NULL;
	if (segmentsFound != 0) {
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
		result = new SegmentedPostingList(splSegments, segmentsFound, true);
#else
		result = new SegmentedPostingList(splSegments, segmentsFound);
#endif
	}
	else {
		if (file != NULL) {
			delete file;
			file = NULL;
		}
		free(splSegments);
		result = new ExtentList_Empty();
	}

	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getPostings2(char*)


ExtentList * CompactIndex::getPostingsForWildcardQuery(const char *pattern, const char *stem) {
	if (header.termCount <= 0)
		return new ExtentList_Empty();

	bool isDocumentLevel = startsWith(pattern, "<!>");

	// extract prefix from given wildcard query
	char *prefix = duplicateString(pattern);
	for (int i = 0; prefix[i] != 0; i++)
		if (IS_WILDCARD_CHAR(prefix[i])) {
			prefix[i] = 0;
			break;
		}
	// check whether the prefix is shorter than 2 chararacter, in which case
	// we refuse to process the query too expensive!)
	if (strlen(prefix) < (isDocumentLevel ? 5 : 2)) {
		free(prefix);
		return new ExtentList_Empty();
	}

	int termsFound = 0, termsAllocated = 256;
	ExtentList **lists = typed_malloc(ExtentList*, termsAllocated);

	// do a binary search in the descriptor list to find the index block in
	// which these terms might appear
	int lower = 0;
	int upper = header.descriptorCount - 1;
	while (upper > lower) {
		int middle = (upper + lower + 1) >> 1;
		int comparison = strcmp(prefix, descriptors[middle].firstTerm);
		if (comparison < 0)
			upper = middle - 1;
		else
			lower = middle;
	} // end while (upper > lower)

	// if it hit the last descriptor, decrease by 1, since the last descriptor
	// only serves as a sentinel and does not refer to actual posting lists
	if (lower == header.descriptorCount - 1)
		lower--;

	bool mustReleaseLock = getLock();
	FileFile *file = getFile();

	char currentTerm[MAX_TOKEN_LENGTH + 1];
	currentTerm[0] = 0;
	SPL_OnDiskSegment *splSegments = typed_malloc(SPL_OnDiskSegment, 1);
	int segmentsFound = 0;
	off_t filePosition = descriptors[lower].blockStart;
	int prefixLen = strlen(prefix);

	// scan inverted file in order to find all terms that match the
	// given (prefix,stem) criterion
	while (filePosition < descriptors[header.descriptorCount - 1].blockEnd) {
		byte buffer[MAX_SEGMENTS_IN_MEMORY * sizeof(PostingListSegmentHeader) + 1024];
		readRawData(filePosition, buffer, sizeof(buffer));
		int localBufferPos = 0;
		char *token = (char*)&buffer[localBufferPos];
		localBufferPos += strlen(token) + 1;
		// use memcpy to extract "segmentCount", as the buffer might not be properly aligned
		int32_t segmentCount = 0;
		memcpy(&segmentCount, &buffer[localBufferPos], sizeof(int32_t));
		localBufferPos += sizeof(int32_t);
#if INDEX_MUST_BE_WORD_ALIGNED
		if (localBufferPos & 7)
			localBufferPos += 8 - (localBufferPos & 7);
#endif

		PostingListSegmentHeader *headers = (PostingListSegmentHeader*)&buffer[localBufferPos];
		localBufferPos += segmentCount * sizeof(PostingListSegmentHeader);
		filePosition += localBufferPos;

		if (strcmp(token, currentTerm) != 0) {
			if (segmentsFound > 0) {
				lists[termsFound] = new SegmentedPostingList(splSegments, segmentsFound);
				if (++termsFound >= termsAllocated) {
					termsAllocated *= 2;
					lists = typed_realloc(ExtentList*, lists, termsAllocated);
				}
				splSegments = typed_malloc(SPL_OnDiskSegment, 1);
				segmentsFound = 0;
			}
			strcpy(currentTerm, token);
		}

		int comparison = strncmp(token, prefix, prefixLen);
		if (comparison > 0)
			break;

		bool meetsCriterion = false;
		if ((comparison == 0) && (fnmatch(pattern, token, 0) == 0)) {
			meetsCriterion = (stem == NULL);
			if (!meetsCriterion) {
				// check if the current term stems to "stem"
				char tempForStemming[MAX_TOKEN_LENGTH * 2];
				strcpy(tempForStemming, token);
				if (isDocumentLevel)
					Stemmer::stemEnglish(&tempForStemming[3]);
				else
					Stemmer::stemEnglish(tempForStemming);
				meetsCriterion = (strcmp(tempForStemming, stem) == 0);
			}
		}

		if (!meetsCriterion)
			for (int i = 0; i < segmentCount; i++)
				filePosition += headers[i].byteLength;
		if (meetsCriterion) {
			splSegments =
				typed_realloc(SPL_OnDiskSegment, splSegments, segmentsFound + segmentCount);
			for (int i = 0; i < segmentCount; i++) {
				assert(headers[i].firstElement <= headers[i].lastElement);
				splSegments[segmentsFound].file = new FileFile(file, filePosition);
				splSegments[segmentsFound].count = headers[i].postingCount;
				splSegments[segmentsFound].byteLength = headers[i].byteLength;
				splSegments[segmentsFound].firstPosting = headers[i].firstElement;
				splSegments[segmentsFound].lastPosting = headers[i].lastElement;
				segmentsFound++;
				filePosition += headers[i].byteLength;
			}
		}
	} // end while (filePosition < descriptors[header.descriptorCount - 1].blockEnd)

	if (segmentsFound > 0)
		lists[termsFound++] = new SegmentedPostingList(splSegments, segmentsFound);
	else
		free(splSegments);

	if (mustReleaseLock)
		releaseLock();
	free(prefix);

	if (termsFound == 0) {
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


FileFile * CompactIndex::getFile() {
	if (inMemoryIndex != NULL)
		return new FileFile(inMemoryIndex, totalSize, false, false);
	else
		return new FileFile(baseFile, (off_t)0);
} // end of getFile()


int CompactIndex::readRawData(off_t where, void *buffer, int len) {
	assert(where >= 0);
	if (inMemoryIndex != NULL) {
		if (where >= totalSize)
			return 0;
		if (where + len > totalSize)
			len = totalSize - where;
		memcpy(buffer, &inMemoryIndex[where], len);
		return len;
	}
	else
		return baseFile->seekAndRead(where, len, buffer);
} // end of readRawData(off_t, void*, int)


byte * CompactIndex::getRawData(off_t where, int len, bool *mustBeFreed) {
	if (inMemoryIndex != NULL) {
		assert(where >= 0);
		assert(where < totalSize);
		assert(where + len <= totalSize);
		*mustBeFreed = false;
		return (byte*)&inMemoryIndex[where];
	}
	else {
		byte *result = (byte*)malloc(len);
		readRawData(where, result, len);
		*mustBeFreed = true;
		return result;
	}
} // end of getRawData(off_t, int, bool)


int CompactIndex::writeRawData(off_t where, void *buffer, int len) {
	assert(!readOnly);
	assert(where >= 0);
	bool mustReleaseLock = getLock();
	lseek(fileHandle, where, SEEK_SET);
	int result = forced_write(fileHandle, buffer, len);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of writeRawData(off_t, void*, int)


char * CompactIndex::getFileName() {
	return duplicateString(fileName);
}


int64_t CompactIndex::getTermCount() {
	return header.termCount;
} // end of getTermCount()


int64_t CompactIndex::getPostingCount() {
	return header.postingCount;
} // end of getPostingCount()


int64_t CompactIndex::getByteSize() {
	bool mustReleaseLock = getLock();
	struct stat buf;
	int64_t result = 0;
	if ((fileHandle >= 0) && (fstat(fileHandle, &buf) == 0))
		result = lseek(fileHandle, (off_t)0, SEEK_END);
	if (mustReleaseLock)
		releaseLock();
	return result;
} // end of getByteSize()


void CompactIndex::getClassName(char *target) {
	strcpy(target, LOG_ID);
}


int CompactIndex::compressPLSH(
		const PostingListSegmentHeader *header, offset referencePosting, byte *buffer) {
	int result = 0;
	result += encodeVByte32(header->postingCount, &buffer[result]);
	result += encodeVByte32(header->byteLength, &buffer[result]);
	result += encodeVByteOffset(header->firstElement - referencePosting, &buffer[result]);
	result += encodeVByteOffset(header->lastElement - header->firstElement, &buffer[result]);
	return result;
} // end of compressPLSH(PostingListSegmentHeader*, offset, byte*)


int CompactIndex::decompressPLSH(
		const byte *buffer, offset referencePosting, PostingListSegmentHeader *header) {
	int result = 0;
	result += decodeVByte32(&header->postingCount, &buffer[result]);
	result += decodeVByte32(&header->byteLength, &buffer[result]);
	offset delta;
	result += decodeVByteOffset(&delta, &buffer[result]);
	header->firstElement = referencePosting + delta;
	result += decodeVByteOffset(&delta, &buffer[result]);
	header->lastElement = header->firstElement + delta;
	return result;
} // end of decompressPLSH(byte*, offset, PostingListSegmentHeader*)



