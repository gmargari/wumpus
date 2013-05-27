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
 * Implementation of the MyInPlaceIndex class.
 *
 * author: Stefan Buettcher
 * created: 2005-11-20
 * changed: 2009-02-01
 **/


#include <string.h>
#include "my_inplace_index.h"
#include "index_compression.h"
#include "segmentedpostinglist.h"
#include "../extentlist/extentlist.h"
#include "../misc/all.h"


const int MyInPlaceIndex::INIT_SEGMENTS_BUFFER_SIZE;
const double MyInPlaceIndex::SEGMENTS_BUFFER_GROWTH_RATE;

static const char * LOG_ID = "MyInPlaceIndex";

static const int64_t BLOCK_SIZE = 1024 * 1024;
static const int64_t INITIAL_BLOCK_COUNT = 64;
static const int64_t MAX_BLOCK_COUNT_PER_TERM = 64;
static const double PREALLOCATION_FACTOR = 2.0;
static const int ALIGNMENT = (1 << 12);


MyInPlaceIndex::MyInPlaceIndex(Index *owner, const char *directory) {
	this->owner = owner;
	this->directory = duplicateString(directory);
	this->fileName = evaluateRelativePathName(directory, "index.long");
	struct stat buf;
	if (stat(fileName, &buf) != 0) {
		// create new index
		fileHandle = open(fileName,
				O_CREAT | O_TRUNC | O_RDWR | O_LARGEFILE, DEFAULT_FILE_PERMISSIONS);
		if (fileHandle < 0) {
			snprintf(errorMessage, sizeof(errorMessage),
					"Unable to create file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			exit(1);
		}

		// allocate initial disk space and initialize free space management
		blockCount = INITIAL_BLOCK_COUNT;
		char *block = (char*)malloc(BLOCK_SIZE);
		for (int i = 0; i < blockCount; i++)
			forced_write(fileHandle, block, BLOCK_SIZE);
		free(block);
		freeMap = (byte*)malloc(blockCount / 8);
		memset(freeMap, 0, blockCount / 8);
		postingCount = 0;
		bytesUsed = 0;
	}
	else {
		// load data from existing index
		fileHandle = open(fileName, O_RDWR | O_LARGEFILE);
		if (fileHandle < 0) {
			snprintf(errorMessage, sizeof(errorMessage), "Unable to open file: %s", fileName);
			log(LOG_ERROR, LOG_ID, errorMessage);
			exit(1);
		}

		// populate term map with on-disk data
		loadTermMap();

		// read header information from data file
		fstat(fileHandle, &buf);
		if (buf.st_size <= sizeof(blockCount)) {
			log(LOG_ERROR, LOG_ID, "In-place index file is empty. This should never happen.");
			exit(1);
		}
		lseek(fileHandle, buf.st_size - sizeof(blockCount) - sizeof(bytesUsed), SEEK_SET);
		forced_read(fileHandle, &blockCount, sizeof(blockCount));
		forced_read(fileHandle, &bytesUsed, sizeof(bytesUsed));
		assert(blockCount % 8 == 0);
		assert(bytesUsed <= blockCount * BLOCK_SIZE);

		// read free space map
		freeMap = (byte*)malloc(1 + blockCount / 8);
		lseek(fileHandle, BLOCK_SIZE * blockCount, SEEK_SET);
		forced_read(fileHandle, freeMap, blockCount / 8);

		// load term descriptors from disk
		postingCount = 0;
		std::map<std::string,InPlaceTermDescriptor>::iterator iter;
		for (iter = termMap->begin(); iter != termMap->end(); ++iter) {
			MyInPlaceTermDescriptor *d = typed_malloc(MyInPlaceTermDescriptor, 1);
			forced_read(fileHandle, d, sizeof(MyInPlaceTermDescriptor));
			d->compressedSegments = (byte*)malloc(d->allocated + 1);
			forced_read(fileHandle, d->compressedSegments, d->allocated);
			iter->second.extra = d;
			postingCount += d->postingCount;
		}

		// make sure we have read everything
		assert(lseek(fileHandle, 0, SEEK_CUR) == buf.st_size - sizeof(blockCount));
	}

	file = new FileFile(fileName, 0, 1);
	currentTerm[0] = 0;
	listUpdateCount = 0;
	relocationCount = 0;

	char value[MAX_CONFIG_VALUE_LENGTH + 1];
	if (!getConfigurationValue("HYBRID_INDEX_MAINTENANCE", value))
		contiguous = true;
	else
		contiguous = !(strcasecmp(value, "NON_CONTIGUOUS_APPEND") == 0);

	if (posix_memalign((void**)&pendingBuffer, ALIGNMENT, MAX_PENDING_DATA + ALIGNMENT) != 0) {
		log(LOG_ERROR, LOG_ID, "posix_memalign failed.");
	}
	pendingData = 0;
	pendingSegmentCount = 0;
} // end of MyInPlaceIndex(Index*, char*, bool)


MyInPlaceIndex::~MyInPlaceIndex() {
	// write official term map to disk
	saveTermMap();
	realFree(pendingBuffer);

	if (contiguous) {
		bytesUsed = blockCount * BLOCK_SIZE;
	}
	if (!contiguous) {
		// if we are in append mode, adjust "blockCount" before saving data to disk
		blockCount = bytesUsed / BLOCK_SIZE + 1;
		if (blockCount % 8 != 0)
			blockCount = (blockCount | 7) + 1;

		char *block = (char*)malloc(BLOCK_SIZE);
		lseek(fileHandle, bytesUsed, SEEK_SET);
		int64_t written = bytesUsed;
		while (written < blockCount * BLOCK_SIZE) {
			int64_t left = blockCount * BLOCK_SIZE - written;
			forced_write(fileHandle, block, MIN(BLOCK_SIZE, left));
			written += MIN(BLOCK_SIZE, left);
		}
		free(block);
		assert(written == blockCount * BLOCK_SIZE);

		free(freeMap);
		freeMap = (byte*)malloc(blockCount / 8);
		memset(freeMap, 255, sizeof(freeMap));
	} // end if (!contiguous)
	
	// write free map to disk
	assert(blockCount % 8 == 0);
	lseek(fileHandle, BLOCK_SIZE * blockCount, SEEK_SET);
	forced_write(fileHandle, freeMap, blockCount / 8);
	free(freeMap);
	freeMap = NULL;

	// write term descriptors to disk
	std::map<std::string,InPlaceTermDescriptor>::iterator iter;
	for (iter = termMap->begin(); iter != termMap->end(); ++iter) {
		MyInPlaceTermDescriptor *myTD = (MyInPlaceTermDescriptor*)iter->second.extra;
		forced_write(fileHandle, myTD, sizeof(MyInPlaceTermDescriptor));
		forced_write(fileHandle, myTD->compressedSegments, myTD->allocated);
		if (myTD->compressedSegments != NULL)
			free(myTD->compressedSegments);
		free(myTD);
		iter->second.extra = NULL;
	}

	// write number of blocks in index; we will need this to find the beginning
	// of the meta-data (free map, etc.) when loading the index from disk at a
	// later point
	forced_write(fileHandle, &blockCount, sizeof(blockCount));
	forced_write(fileHandle, &bytesUsed, sizeof(bytesUsed));

	free(fileName);
	fileName = NULL;
	close(fileHandle);
	delete file;

	printSummary();
} // end of ~MyInPlaceIndex()


ExtentList * MyInPlaceIndex::getPostings(const char *term) {
	LocalLock lock(this);

	if (pendingSegmentCount > 0)
		flushPendingData();

	if ((strchr(term, '*') != NULL) || (strchr(term, '$') != NULL)) {
		log(LOG_ERROR, LOG_ID, "Stemming and prefix queries not supported by this index.");
		return new ExtentList_Empty();
	}

	std::map<std::string,InPlaceTermDescriptor>::iterator iter = termMap->find(term);
	if (iter == termMap->end())
		return new ExtentList_Empty();

	MyInPlaceTermDescriptor *myTD = (MyInPlaceTermDescriptor*)iter->second.extra;
	if (myTD->segmentCount == 0)
		return new ExtentList_Empty();

	int segmentCount = myTD->segmentCount;
	MyInPlaceSegmentHeader *headers = typed_malloc(MyInPlaceSegmentHeader, segmentCount);
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
	SPL_InMemorySegment *segments = typed_malloc(SPL_InMemorySegment, segmentCount);
#else
	SPL_OnDiskSegment *segments = typed_malloc(SPL_OnDiskSegment, segmentCount);
#endif

	// decompress segment headers and copy data to a new sequence of segment
	// headers that will be understood by SegmentedPostingList
	decompressSegmentHeaders(myTD->compressedSegments, segmentCount, myTD->allocated, headers);
	for (int i = 0; i < segmentCount; i++) {
		segments[i].firstPosting = headers[i].firstPosting;
		segments[i].lastPosting = headers[i].lastPosting;
		segments[i].byteLength = headers[i].size;
		segments[i].count = headers[i].postingCount;
#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
		segments[i].postings = (byte*)malloc(segments[i].byteLength);
		lseek(fileHandle, headers[i].filePosition, SEEK_SET);
		forced_read(fileHandle, segments[i].postings, segments[i].byteLength);
#else
		segments[i].file = new FileFile(file, headers[i].filePosition);
#endif
	}
	free(headers);

#if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
	return new SegmentedPostingList(segments, segmentCount, true);
#else
	return new SegmentedPostingList(segments, segmentCount);
#endif
} // end of getPostings(char*)


void MyInPlaceIndex::addPostings(const char *term, offset *postings, int count) {
	LocalLock lock(this);
	assert(postings[count - 1] >= postings[0]);

	if (count <= 0)
		return;
	if (count > MAX_SEGMENT_SIZE) {
		// if this is too large for a single segment, split up into two lists
		addPostings(term, postings, count / 2);
		addPostings(term, &postings[count / 2], count - count / 2);
		return;
	}

	// compress postings and defer to handler for compressed list
	int size;
	byte *compressed = compressorForID[INDEX_COMPRESSION_MODE](postings, count, &size);
	addPostings(term, compressed, size, count, postings[0], postings[count - 1]);
	free(compressed);
} // end of addPostings(char*, offset*, int)


void MyInPlaceIndex::addPostings(
		const char *term, byte *postings, int size, int count, offset first, offset last) {
	LocalLock lock(this);
	assert(last >= first);
	assert(count <= MAX_SEGMENT_SIZE);

	if (currentTerm[0] == 0) {
		strcpy(currentTerm, term);
		listUpdateCount++;
	}
	else if (strcmp(term, currentTerm) != 0) {
		// current term has changed; flush all pending data for previous term
		flushPendingData();
		strcpy(currentTerm, term);
		listUpdateCount++;
	}

	// make sure we have enough space in the input buffer
	if ((pendingSegmentCount >= MAX_PENDING_SEGMENT_COUNT) ||
	    (pendingData + size + 2 * ALIGNMENT > MAX_PENDING_DATA)) {
		flushPendingData();
		strcpy(currentTerm, term);
	}

	if (pendingSegmentCount > 0) {
		// if we already have a segment for the current term, check whether we need to
		// merge the new segment with the previous one
		MyInPlaceSegmentHeader *prevHeader = &pendingSegments[pendingSegmentCount - 1];
		if ((prevHeader->postingCount < MIN_SEGMENT_SIZE) || (count < MIN_SEGMENT_SIZE)) {
			if (prevHeader->postingCount + count > MAX_SEGMENT_SIZE) {
				// their combined size is too much for a single segment: split up
				offset *uncompressed = typed_malloc(offset, prevHeader->postingCount + count);
				int cnt;
				decompressList(prevHeader->compressedPostings, prevHeader->size, &cnt, uncompressed);
				assert(cnt == prevHeader->postingCount);
				decompressList(postings, size, &cnt, &uncompressed[cnt]);
				assert(cnt == count);
				pendingData -= prevHeader->size;
				pendingSegmentCount--;
				addPostings(term, uncompressed, prevHeader->postingCount + count);
				free(uncompressed);
			}
			else {
				// their combined size is small enough: merge into a single segment
				int newLen, newByteLen;
				mergeCompressedLists(
						prevHeader->compressedPostings, prevHeader->size,
						postings, size,
						prevHeader->lastPosting, &newLen, &newByteLen, true);
				assert(newLen == prevHeader->postingCount + count);
				pendingData = pendingData - prevHeader->size + newByteLen;
				prevHeader->lastPosting = last;
				prevHeader->postingCount = newLen;
				prevHeader->size = newByteLen;
			}
			return;
		}
	} // end if (pendingSegmentCount > 0)

	MyInPlaceSegmentHeader *header = &pendingSegments[pendingSegmentCount];
	header->firstPosting = first;
	header->lastPosting = last;
	header->postingCount = count;
	header->size = size;
	header->compressedPostings = &pendingBuffer[pendingData];
	memcpy(header->compressedPostings, postings, size);
	pendingData += size;
	pendingSegmentCount++;
} // end of addPostings(char*, byte*, int, int, offset, offset)


void MyInPlaceIndex::markBlocks(int start, int count, char value) {
	for (int i = start; i < start + count; i++) {
		freeMap[i >> 3] |= (1 << (i & 7));
		if (!value)
			freeMap[i >> 3] ^= (1 << (i & 7));
	}
} // end of markBlocks(int, int, char)


void MyInPlaceIndex::freeBlocks(int start, int count) {
	markBlocks(start, count, 0);
}


int MyInPlaceIndex::allocateBlocks(int count) {
	static bool initialized = false;
	static byte freeBlocksHere[256];
	if (!initialized) {
		// initialize "freeBlocksHere" table for quick lookups of how many free bits
		// we have in a given byte
		for (int i = 0; i < 256; i++) {
			int freeHere = 8;
			for (int k = 0; k < 8; k++)
				if (i & (1 << k))
					freeHere--;
			freeBlocksHere[i] = freeHere;
		}
		initialized = true;
	}

	int end = blockCount / 8;
	if (count == 1) {
		for (int i = 0; i < end; i++) {
			if (freeMap[i] != 255) {
				for (int k = 0; k < 8; k++)
					if (!(freeMap[i] & (1 << k))) {
						freeMap[i] |= (1 << k);
						return i * 8 + k;
					}
			}
		}
	}
	else if (count <= 8) {
		for (int i = 0; i < end - 1; i++)
			if (freeBlocksHere[freeMap[i]] + freeBlocksHere[freeMap[i + 1]] >= count) {
				int s = i * 8;
				int e = s + 15;
				int cnt = 0;
				for (int k = s; k <= e; k++) {
					if (freeMap[k >> 3] & (1 << (k & 7)))
						cnt = 0;
					else if (++cnt >= count) {
						s = k - count + 1;
						markBlocks(s, count, 1);
						return s;
					}
				}
			}
	}
	else {
		int cnt = 0;
		for (int i = 0; i < end; i++) {
			if (freeMap[i] != 0)
				cnt = 0;
			else {
				cnt++;
				if (cnt * 8 >= count) {
					int s = (i - cnt + 1) * 8;
					markBlocks(s, count, 1);
					return s;
				}
			}
		}
	}

	// create new blocks, at least "count" of them
	int newBlocks = (count | (INITIAL_BLOCK_COUNT - 1)) + 1;
	freeMap = (byte*)realloc(freeMap, (blockCount + newBlocks) / 8);
	markBlocks(blockCount, newBlocks, 0);
	blockCount += newBlocks;
	forced_ftruncate(fileHandle, blockCount * BLOCK_SIZE);

	// re-run allocation procedure
	return allocateBlocks(count);
} // end of allocateBlocks(int)


MyInPlaceTermDescriptor * MyInPlaceIndex::createNewDescriptor(int64_t spaceNeeded) {
	MyInPlaceTermDescriptor *result = typed_malloc(MyInPlaceTermDescriptor, 1);
	result->segmentCount = 0;
	result->allocated = 0;
	result->compressedSegments = (byte*)malloc(1);
	if (contiguous) {
		result->indexBlockStart = allocateBlocks((2 * spaceNeeded) / BLOCK_SIZE + 1) * BLOCK_SIZE;
		result->indexBlockLength = BLOCK_SIZE;
	}
	else {
		result->indexBlockStart = 0;
		result->indexBlockLength = 0;
	}
	result->indexBlockUsed = 0;
	result->postingCount = 0;
	return result;
} // end of createNewDescriptor(int64_t)


MyInPlaceTermDescriptor * MyInPlaceIndex::getDescriptorOrCreate(const char *term, int64_t spaceNeeded) {
	InPlaceTermDescriptor *descriptor = getDescriptor(term);
	if (descriptor != NULL)
		return (MyInPlaceTermDescriptor*)descriptor->extra;
	else {
		InPlaceTermDescriptor d;
		MyInPlaceTermDescriptor *desc = createNewDescriptor(spaceNeeded);
		strcpy(d.term, term);
		d.appearsInIndex = 0;
		d.extra = desc;
		(*termMap)[term] = d;
		return desc;
	}
} // end of getDescriptorOrCreate(char*, int64_t)


void MyInPlaceIndex::relocatePostings(MyInPlaceTermDescriptor *desc, int64_t spaceNeeded) {
	// allocate new space somewhere
	assert(desc->indexBlockLength % BLOCK_SIZE == 0);
	assert(desc->indexBlockStart % BLOCK_SIZE == 0);
	int oldBlockCount = desc->indexBlockLength / BLOCK_SIZE;
	int oldStart = desc->indexBlockStart / BLOCK_SIZE;
	int blockCount = (int)((desc->indexBlockUsed + spaceNeeded) * PREALLOCATION_FACTOR);
	blockCount = MAX(oldBlockCount + 2, blockCount / BLOCK_SIZE);
	int start = allocateBlocks(blockCount);

	// copy list segments from old position to new position
	char *buffer = (char*)malloc(BLOCK_SIZE);
	for (int i = 0; i * BLOCK_SIZE < desc->indexBlockLength; i++) {
		lseek(fileHandle, desc->indexBlockStart + i * BLOCK_SIZE, SEEK_SET);
		forced_read(fileHandle, buffer, BLOCK_SIZE);
		lseek(fileHandle, (start + i) * BLOCK_SIZE, SEEK_SET);
		forced_write(fileHandle, buffer, BLOCK_SIZE);
	}
	free(buffer);

	if (desc->segmentCount > 0) {
		// adjust file pointers in all affected segment headers
		MyInPlaceSegmentHeader *headers =
			typed_malloc(MyInPlaceSegmentHeader, desc->segmentCount);
		decompressSegmentHeaders(
				desc->compressedSegments, desc->segmentCount, desc->allocated, headers);
		int64_t delta = (start * BLOCK_SIZE) - desc->indexBlockStart;
		for (int i = 0; i < desc->segmentCount; i++) {
			int64_t fp = headers[i].filePosition;
			if ((fp >= desc->indexBlockStart) && (fp < desc->indexBlockStart + desc->indexBlockLength))
				headers[i].filePosition += delta;
		}
		free(desc->compressedSegments);
		desc->compressedSegments =
			(byte*)malloc(desc->segmentCount * sizeof(MyInPlaceSegmentHeader));
		int newSize;
		compressSegmentHeaders(headers, desc->segmentCount, desc->compressedSegments, &newSize);
		desc->compressedSegments = (byte*)realloc(desc->compressedSegments, newSize + 1);
		desc->allocated = newSize;
		free(headers);
	}

	// update internal state
	desc->indexBlockStart = start * BLOCK_SIZE;
	desc->indexBlockLength = blockCount * BLOCK_SIZE;

	// mark old location as free
	freeBlocks(oldStart, oldBlockCount);

	relocationCount++;
} // end of relocatePostings(MyInPlaceTermDescriptor*, int64_t)


void MyInPlaceIndex::allocateViaChaining(MyInPlaceTermDescriptor *desc, int64_t spaceNeeded) {
	int newBlockCount = 1;
	while (newBlockCount * BLOCK_SIZE < spaceNeeded)
		newBlockCount++;
	int newStart = allocateBlocks(newBlockCount);
	desc->indexBlockStart = newStart * BLOCK_SIZE;
	desc->indexBlockLength = newBlockCount * BLOCK_SIZE;
	desc->indexBlockUsed = 0;

	relocationCount++;
} // end of allocateViaChaining(MyInPlaceTermDescriptor*, int64_t)


void MyInPlaceIndex::flushPendingData() {
	if (pendingSegmentCount <= 0) {
		pendingSegmentCount = 0;
		pendingData = 0;
		currentTerm[0] = 0;
		return;
	}

	assert(currentTerm[0] != 0);

	int64_t spaceNeeded = 0;
	for (int i = 0; i < pendingSegmentCount; i++) {
		spaceNeeded += pendingSegments[i].size;
		postingCount += pendingSegments[i].postingCount;
	}
	spaceNeeded += 2 * ALIGNMENT;

	MyInPlaceTermDescriptor *desc = getDescriptorOrCreate(currentTerm, spaceNeeded);
	for (int i = 0; i < pendingSegmentCount; i++)
		desc->postingCount += pendingSegments[i].postingCount;

	// Check whether we have sufficient space for the incoming postings; if not,
	// then relocate list in order to get more space.
	// But: Only do this if the currently allocated chunk is less than
	// MAX_BLOCK_COUNT_PER_TERM. If it is more, then we will use chaining instead.
	if (contiguous)
		if (spaceNeeded > desc->indexBlockLength - desc->indexBlockUsed)
			if (desc->indexBlockLength < MAX_BLOCK_COUNT_PER_TERM * BLOCK_SIZE)
				relocatePostings(desc, spaceNeeded);

	MyInPlaceSegmentHeader *headers =
		typed_malloc(MyInPlaceSegmentHeader, desc->segmentCount + pendingSegmentCount);
	decompressSegmentHeaders(
			desc->compressedSegments, desc->segmentCount, desc->allocated, headers);

	if (!contiguous) {
		lseek(fileHandle, bytesUsed, SEEK_SET);
		// set meta-data for current list segment
		for (int i = 0; i < pendingSegmentCount; i++) {
			MyInPlaceSegmentHeader *header = &headers[desc->segmentCount++];
			*header = pendingSegments[i];
			header->filePosition = bytesUsed;
			header->compressedPostings = NULL;
			bytesUsed += header->size;
		}
		// make sure the output data are properly aligned (multiple of FS block size)
		if (pendingData & (ALIGNMENT - 1)) {
			int adjustment = ALIGNMENT - (pendingData & (ALIGNMENT - 1));
			pendingData += adjustment;
			bytesUsed += adjustment;
		}
		// write data
		assert(pendingData % ALIGNMENT == 0);
		forced_write(fileHandle, pendingBuffer, pendingData);
	} // end if (!contiguous)

	if (contiguous) {

		if ((pendingSegmentCount <= 1) && (desc->segmentCount > 0)) {
			MyInPlaceSegmentHeader *prevHeader = &headers[desc->segmentCount - 1];
			bool previousIsInCurrentChunk = false;
			if (prevHeader->filePosition >= desc->indexBlockStart)
				if (prevHeader->filePosition < desc->indexBlockStart + desc->indexBlockUsed)
					previousIsInCurrentChunk = true;

			// check whether one of the two segments (previous or incoming) is too small
			// to survive independently; if that's the case, we have to merge them
			int oldPCnt = prevHeader->postingCount;
			int newPCnt = pendingSegments[0].postingCount;
			if ((previousIsInCurrentChunk) && ((oldPCnt < MIN_SEGMENT_SIZE) || (newPCnt < MIN_SEGMENT_SIZE))) {
				offset *uncompressed = typed_malloc(offset, oldPCnt + newPCnt);
				byte *oldCompressed = (byte*)malloc(prevHeader->size);

				lseek(fileHandle, prevHeader->filePosition, SEEK_SET);
				forced_read(fileHandle, oldCompressed, prevHeader->size);

				int cnt;
				decompressList(oldCompressed, prevHeader->size, &cnt, uncompressed);
				assert(cnt == oldPCnt);
				decompressList(pendingSegments[0].compressedPostings,
						pendingSegments[0].size, &cnt, &uncompressed[oldPCnt]);
				assert(cnt == newPCnt);

				// remove previous segment from list of pending segments; replace by new one
				desc->indexBlockUsed = prevHeader->filePosition - desc->indexBlockStart;
				desc->segmentCount--;
				pendingSegmentCount = 0;
				pendingData = 0;
				addPostings(currentTerm, uncompressed, oldPCnt + newPCnt);

				free(oldCompressed);
				free(uncompressed);
			}
			
		} // end if ((pendingSegmentCount == 1) && (desc->segmentCount > 0))

		if (pendingSegmentCount > 1) {
			if (desc->indexBlockUsed & (ALIGNMENT - 1))
				desc->indexBlockUsed = (desc->indexBlockUsed | (ALIGNMENT - 1)) + 1;
			assert(desc->indexBlockUsed % ALIGNMENT == 0);
		}
		int firstUnflushed = desc->segmentCount;
		int toWrite = 0;

		for (int i = 0; i < pendingSegmentCount; i++) {
			if (desc->indexBlockUsed + pendingSegments[i].size + ALIGNMENT > desc->indexBlockLength) {
				// this will only happen if the list currently occupies at least
				// MAX_BLOCK_COUNT_PER_TERM blocks in the on-disk index; in that case,
				// do not relocate any more, but allocate a new chunk and chain to it
				if (firstUnflushed < desc->segmentCount) {
					lseek(fileHandle, headers[firstUnflushed].filePosition, SEEK_SET);
					if (toWrite & (ALIGNMENT - 1)) {
						int adjustment = ALIGNMENT - (toWrite & (ALIGNMENT - 1));
						toWrite += adjustment;
						desc->indexBlockUsed += adjustment;
					}
					assert(toWrite % ALIGNMENT == 0);
					forced_write(fileHandle, headers[firstUnflushed].compressedPostings, toWrite);
					firstUnflushed = desc->segmentCount;
					toWrite = 0;
				}
				allocateViaChaining(desc, MAX(16 * BLOCK_SIZE, spaceNeeded));
			} // end [need to allocate via chaining]

			// set meta-data for current list segment
			MyInPlaceSegmentHeader *header = &headers[desc->segmentCount++];
			*header = pendingSegments[i];
			header->filePosition = desc->indexBlockStart + desc->indexBlockUsed;
			desc->indexBlockUsed += header->size;
			toWrite += header->size;
			assert(desc->indexBlockUsed <= desc->indexBlockLength);
		} // end for (int i = 0; i < pendingSegmentCount; i++)

		if (firstUnflushed < desc->segmentCount) {
			lseek(fileHandle, headers[firstUnflushed].filePosition, SEEK_SET);
			if (pendingSegmentCount > 1) {
				if (toWrite & (ALIGNMENT - 1)) {
					int adjustment = ALIGNMENT - (toWrite & (ALIGNMENT - 1));
					toWrite += adjustment;
					desc->indexBlockUsed += adjustment;
				}
				assert(toWrite % ALIGNMENT == 0);
			}
			forced_write(fileHandle, headers[firstUnflushed].compressedPostings, toWrite);
			firstUnflushed = desc->segmentCount;
			toWrite = 0;
		}
	} // end if (contiguous)

	// re-compress segment descriptors for current term
	for (int i = 0; i < desc->segmentCount; i++)
		headers[i].compressedPostings = NULL;
	int size;
	free(desc->compressedSegments);
	desc->compressedSegments =
		(byte*)malloc(desc->segmentCount * sizeof(MyInPlaceSegmentHeader));
	compressSegmentHeaders(headers, desc->segmentCount, desc->compressedSegments, &size);
	desc->allocated = size;
	desc->compressedSegments = (byte*)realloc(desc->compressedSegments, MAX(size, 1));
	free(headers);

	pendingData = 0;
	pendingSegmentCount = 0;
	currentTerm[0] = 0;
} // end of flushPendingData()


int64_t MyInPlaceIndex::getTermCount() {
	LocalLock lock(this);
	return termMap->size();
}


int64_t MyInPlaceIndex::getByteSize() {
	LocalLock lock(this);
	return BLOCK_SIZE * blockCount;
}


int64_t MyInPlaceIndex::getPostingCount() {
	LocalLock lock(this);
	return postingCount;
}


char * MyInPlaceIndex::getFileName() {
	LocalLock lock(this);
	return duplicateString(fileName);
} // end of getFileName()


void MyInPlaceIndex::compressSegmentHeaders(
		MyInPlaceSegmentHeader *headers, int count, byte *output, int *size) {
	int outPos = 0;
	offset prevPosting = 0;
	for (int i = 0; i < count; i++) {
		outPos += encodeVByteOffset(headers[i].filePosition, &output[outPos]);
		outPos += encodeVByte32(headers[i].postingCount, &output[outPos]);
		outPos += encodeVByte32(headers[i].size, &output[outPos]);
		outPos += encodeVByteOffset(headers[i].firstPosting - prevPosting, &output[outPos]);
		outPos += encodeVByteOffset(headers[i].lastPosting - headers[i].firstPosting, &output[outPos]);
		prevPosting = headers[i].lastPosting;
	}
	*size = outPos;
} // end of compressSegmentHeaders(...)


void MyInPlaceIndex::decompressSegmentHeaders(
		byte *compressed, int count, int size, MyInPlaceSegmentHeader *output) {
	int inPos = 0;
	offset prevPosting = 0;
	for (int i = 0; i < count; i++) {
		inPos += decodeVByteOffset(&output[i].filePosition, &compressed[inPos]);
		inPos += decodeVByte32(&output[i].postingCount, &compressed[inPos]);
		inPos += decodeVByte32(&output[i].size, &compressed[inPos]);
		inPos += decodeVByteOffset(&output[i].firstPosting, &compressed[inPos]);
		output[i].firstPosting += prevPosting;
		inPos += decodeVByteOffset(&output[i].lastPosting, &compressed[inPos]);
		output[i].lastPosting += output[i].firstPosting;
		prevPosting = output[i].lastPosting;
	}
	if (inPos != size) {
		sprintf(errorMessage, "decompressSegmentHeaders(%p): count = %d, inPos = %d, size = %d. Problem.",
				compressed, count, inPos, size);
		log(LOG_ERROR, LOG_ID, errorMessage);
		assert(false);
	}
} // end of decompressSegmentHeaders(...)


void MyInPlaceIndex::finishUpdate() {
	LocalLock lock(this);
	flushPendingData();
	fsync(fileHandle);
	close(fileHandle);
	fileHandle = open(fileName, O_RDWR | O_LARGEFILE);
	assert(fileHandle >= 0);
	printSummary();
} // end of finishUpdate()


void MyInPlaceIndex::printSummary() {
	sprintf(errorMessage,
			"Number of list update operations performed: %u.", listUpdateCount);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage,
			"Number of list relocations performed: %u.", relocationCount);
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage,
			"Index contents: %lld postings for %d terms.",
			static_cast<long long>(postingCount), static_cast<int>(termMap->size()));
	log(LOG_DEBUG, LOG_ID, errorMessage);
	long long bytesRead, bytesWritten;
	getReadWriteStatistics(&bytesRead, &bytesWritten);
	sprintf(errorMessage,
			"Bytes read: %lld. Bytes written: %lld. Total: %lld.\n",
			bytesRead, bytesWritten, bytesRead + bytesWritten);
	log(LOG_DEBUG, LOG_ID, errorMessage);
} // end of printSummary()
	
