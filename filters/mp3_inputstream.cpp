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
 * author: Stefan Buettcher
 * created: 2005-03-19
 * changed: 2005-07-21
 **/


#include "mp3_inputstream.h"
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "../misc/all.h"


static char * extractElement(char *source, int off, int len, char *target) {
	memcpy(target, &source[off], len);
	target[len] = 0;
	len = strlen(target);
	if (len > 1) {
		while ((len > 1) && (target[len - 1] == ' '))
			target[--len] = 0;
	}
	while ((*target != 0) && (*target == ' '))
		target++;
	return target;
} // end of extractElement(char*, int, int, char*)


MP3InputStream::MP3InputStream(const char *fileName) {
	if (fileName == NULL)
		inputFile = -1;
	else if (fileName[0] == 0)
		inputFile = 0;
	else
		inputFile = open(fileName, O_RDONLY);
	initialize();
} // end of MP3InputStream(char*)


MP3InputStream::MP3InputStream(int fd) {
	inputFile = fd;
	initialize();
} // end of MP3InputStream(int)


void MP3InputStream::initialize() {
	tempFileName[0] = 0;
	if (inputFile >= 0) {
		char tagBuffer[ID3_TAG_SIZE], temp[ID3_TAG_SIZE];
		struct stat buf;
		fstat(inputFile, &buf);
		lseek(inputFile, buf.st_size - ID3_TAG_SIZE, SEEK_SET);
		forced_read(inputFile, tagBuffer, ID3_TAG_SIZE);
		close(inputFile);

		char xmlBuffer[ID3_TAG_SIZE * 2];
		int outLen = 0;
		outLen += sprintf(&xmlBuffer[outLen], "<title>%s</title> ",
		                  extractElement(tagBuffer, 3, 30, temp));
		outLen += sprintf(&xmlBuffer[outLen], "<artist>%s</artist> ",
		                  extractElement(tagBuffer, 33, 30, temp));
		outLen += sprintf(&xmlBuffer[outLen], "<album>%s</album> ",
		                  extractElement(tagBuffer, 63, 30, temp));
		outLen += sprintf(&xmlBuffer[outLen], "<year>%s</year> ",
		                  extractElement(tagBuffer, 93, 4, temp));
		outLen += sprintf(&xmlBuffer[outLen], "<comment>%s</comment> ",
		                  extractElement(tagBuffer, 97, 28, temp));
		sprintf(tempFileName, "%s/%s", TEMP_DIRECTORY, "index-conversion-XXXXXXXX.mp3");
		randomTempFileName(tempFileName);
		inputFile = open(tempFileName, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		xmlBuffer[outLen] = 0;
		forced_write(inputFile, xmlBuffer, strlen(xmlBuffer));
		lseek(inputFile, (off_t)0, SEEK_SET);
		XMLInputStream::initialize();
	}
} // end of initialize()


MP3InputStream::~MP3InputStream() {
	if (inputFile >= 0) {
		close(inputFile);
		inputFile = -1;
	}
	if (tempFileName[0] != 0)
		unlink(tempFileName);
} // end of ~MP3InputStream()


int MP3InputStream::getDocumentType() {
	return DOCUMENT_TYPE_MPEG;
} // end of getDocumentType()


bool MP3InputStream::canProcess(const char *fileName, byte *fileStart, int length) {
	if (length < 512)
		return false;
	if (fileStart[0] != 255)
		return false;
	if (fileStart[1] < 4)
		return false;
	char id3Buffer[128];
	int fd = open(fileName, O_RDONLY);
	if (fd < 0)
		return false;
	struct stat buf;
	fstat(fd, &buf);
	lseek(fd, buf.st_size - 128, SEEK_SET);
	forced_read(fd, id3Buffer, 128);
	close(fd);
	if (strncmp(id3Buffer, "TAG", 3) != 0)
		return false;
	if ((id3Buffer[3] != 0) || (id3Buffer[33] != 0) || (id3Buffer[63] != 0) ||
	    (id3Buffer[93] != 0))
		return true;
	return false;
} // end of canProcess(char*, byte*, int)



