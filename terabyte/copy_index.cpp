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
 * Copies a CompactIndex instance to a new file. We need this in order to copy
 * a CompactIndex to a raw partition, whose file size we cannot change and which
 * therefore needs the file header and the interval descriptors (which are at
 * the end of the file) to be relocated.
 *
 * author: Stefan Buettcher
 * created: 2005-06-14
 * changed: 2006-09-19
 **/


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../index/compactindex.h"
#include "../misc/all.h"


// If this is true, then the data are written at the end of the target index.
// Unfortunately, this is an important aspect, as disk transfer rates are
// usually higher in the outer regions of a hard drive (i.e., end of raw
// partition instead of start).
static const bool COPY_TO_END = true;

static long long inputIndexSize;
static long long outputIndexSize;
static long long totalBytesWritten = 0;


void printUsage() {
	fprintf(stderr, "Usage:  copy_index INPUT_INDEX OUTPUT_INDEX\n\n");
	fprintf(stderr, "Both files, INPUT_INDEX and OUTPUT_INDEX, have to exist already. The size of ");
	fprintf(stderr, "the output index has to be bigger than the size of the input index. It is not ");
	fprintf(stderr, "changed during the copying process.\n\n");
	exit(1);
} // end of printUsage()


void copyEverything(int inFD, int outFD) {
	lseek(inFD, (off_t)0, SEEK_SET);
	if (COPY_TO_END)
		lseek(outFD, (off_t)(outputIndexSize - inputIndexSize), SEEK_SET);
	else
		lseek(outFD, (off_t)0, SEEK_SET);
	static const int BUFFER_SIZE = 2 * 1024 * 1024;
	char buffer[BUFFER_SIZE];
	int bufferSize = read(inFD, buffer, BUFFER_SIZE);
	while (bufferSize > 0) {
		int written = write(outFD, buffer, bufferSize);
		if (written != bufferSize)
			perror(NULL);
		assert(written == bufferSize);
		totalBytesWritten += written;
		if (totalBytesWritten % (32 * 1024 * 1024) == 0) {
			int mb = totalBytesWritten / (1024 * 1024);
			printf("Data read/written: %d MB\n", mb);
		}
		bufferSize = read(inFD, buffer, BUFFER_SIZE);
	}
} // end of copyEverything(int, int)


void copyHeaderAndDescriptors(int inFD, int outFD) {
	CompactIndexHeader header;
	struct stat inBuf;
	fstat(inFD, &inBuf);
	lseek(inFD, inBuf.st_size - sizeof(header), SEEK_SET);
	struct stat outBuf;
	fstat(outFD, &outBuf);
	lseek(outFD, outputIndexSize - sizeof(header), SEEK_SET);
	read(inFD, &header, sizeof(header));
	write(outFD, &header, sizeof(header));
	int descriptorCount = header.descriptorCount;
	int totalDescriptorSize = descriptorCount * sizeof(CompactIndexIntervalDescriptor);
	printf("descriptorCount = %d\n", descriptorCount);
	lseek(inFD, inBuf.st_size - sizeof(header) - totalDescriptorSize, SEEK_SET);
	lseek(outFD, outputIndexSize - sizeof(header) - totalDescriptorSize, SEEK_SET);
	for (int i = 0; i < descriptorCount; i++) {
		CompactIndexIntervalDescriptor descriptor;
		int gelesen = read(inFD, &descriptor, sizeof(descriptor));
		if (COPY_TO_END) {
			// adjust file offsets in case we are copying to end of target index
			descriptor.intervalStart += (outputIndexSize - inputIndexSize);
			descriptor.intervalEnd += (outputIndexSize - inputIndexSize);
		}
		int geschrieben = write(outFD, &descriptor, sizeof(descriptor));
		assert(gelesen == geschrieben);
		totalBytesWritten += geschrieben;
	}
	int mb = totalBytesWritten / (1024 * 1024);
	printf("Data read/written: %d MB\n", mb);
} // end of copyHeaderAndDescriptors(int, int)


int main(int argc, char **argv) {
	if (argc != 3)
		printUsage();
	struct stat inputFile;
	if (stat(argv[1], &inputFile) != 0) {
		fprintf(stderr, "Input file does not exist: %s\n", argv[1]);
		return 1;
	}
	struct stat outputFile;
	if (stat(argv[2], &outputFile) != 0) {
		fprintf(stderr, "Output file does not exist: %s\n", argv[2]);
		return 1;
	}
	int inFD = open(argv[1], O_RDONLY);
	assert(inFD >= 0);
	inputIndexSize = lseek(inFD, (off_t)0, SEEK_END);
	int outFD = open(argv[2], O_RDWR | O_SYNC | O_LARGEFILE);
	assert(outFD >= 0);
	outputIndexSize = lseek(outFD, (off_t)0, SEEK_END);
	if (inputIndexSize > outputIndexSize) {
		fprintf(stderr, "Input file is bigger than output file. Unable to copy!\n");
		fprintf(stderr, "%lld, %lld\n", inputFile.st_size, outputIndexSize);
		return 1;
	}
	printf("Output index size: %lld\n", outputIndexSize);
	copyEverything(inFD, outFD);
	copyHeaderAndDescriptors(inFD, outFD);
	close(inFD);
	close(outFD);
	return 0;
} // end of main(int, char**)



