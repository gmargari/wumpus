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
 * Definition of basic data structures and types, such as addresses and extents.
 *
 * author: Stefan Buettcher
 * created: 2004-09-02
 * changed: 2011-09-25
 **/


#ifndef __INDEX__INDEX_TYPES_H
#define __INDEX__INDEX_TYPES_H


#include <inttypes.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../config/config.h"
#include "../misc/macros.h"


// the following are used by most functions that return a status code
static const int RESULT_SUCCESS = 0;
static const int RESULT_ERROR = 1;

/** These are error codes used by the Index class. **/
// query syntax error
static const int ERROR_SYNTAX_ERROR = 2;
// when trying to process a query while in shutdown state
static const int ERROR_SHUTTING_DOWN = 3;
// when trying to index something that is not a regular file
static const int ERROR_NO_SUCH_FILE = 4;
// when trying to index something in a forbidden directory
static const int ERROR_DIR_NOT_ALLOWED = 5;
// if no suitable FilteredInputStream implementation can be found
static const int ERROR_UNKNOWN_FILE_FORMAT = 6;
// input tokenizer returns 0 tokens
static const int ERROR_EMPTY_FILE = 7;
// insufficient permissions to access resource
static const int ERROR_ACCESS_DENIED = 8;
// @addfile failed because file has not been changed since last update
static const int ERROR_FILE_UNCHANGED = 9;
// @addfile failed because the file that should be added is too small
static const int ERROR_FILE_TOO_SMALL = 10;
// @addfile failed because the file that should be added is too large
static const int ERROR_FILE_TOO_LARGE = 11;
// when trying to update a read-only index
static const int ERROR_READ_ONLY = 12;
// when somebody tries to run two update operations at the same time
static const int ERROR_CONCURRENT_UPDATE = 13;
// unspecified internal error
static const int ERROR_INTERNAL_ERROR = 14;

static const int MAX_ERROR_CODE = 14;

extern const char * ERROR_MESSAGES[MAX_ERROR_CODE + 2];

// @getfile does not send response because file is too big
static const int MAX_GETFILE_FILE_SIZE = 32 * 1024 * 1024;

#define printErrorMessage(statusCode, buffer) { \
	if (statusCode < 0) strcpy(buffer, "Error"); \
	else if (statusCode > MAX_ERROR_CODE) strcpy(buffer, "Error"); \
	else strcpy(buffer, ERROR_MESSAGES[statusCode]); \
}


typedef unsigned char byte;

#if INDEX_OFFSET_BITS == 64
	typedef int64_t offset;
	static const offset MAX_OFFSET = (1LL << 47) - 1;
	#define OFFSET_FORMAT "%"PRId64
#elif INDEX_OFFSET_BITS == 32
	typedef int32_t offset;
	static const offset MAX_OFFSET = 0x7F000000;
	#define OFFSET_FORMAT "%"PRId32
#else
	#error "Size of index offsets undefined. Choose either 32 or 64."
#endif


static const int MAX_INT = 0x7FFFFFFF;
static const offset ONE = 1;
static const offset TWO = 2;

/**
 * When using document-level indexing, we encode the TF for a given term in the
 * least significant K bits of each posting. The value of K corresponds to a
 * maximum TF value that can be encoded this way. That maximum TF value is defined
 * here: 0..31.
 **/
static const int DOC_LEVEL_SHIFT = 5;
static const offset DOC_LEVEL_MAX_TF = 0x1F;
static const offset DOC_LEVEL_ENCODING_THRESHOLD = 0x10;
static const double DOC_LEVEL_ENCODING_THRESHOLD_DOUBLE = DOC_LEVEL_ENCODING_THRESHOLD;
static const double DOC_LEVEL_BASE = 1.15;

static inline offset encodeDocLevelTF(offset tf) {
	if (tf < DOC_LEVEL_ENCODING_THRESHOLD)
		return tf;
	long result = DOC_LEVEL_ENCODING_THRESHOLD +
		LROUND(log(tf / DOC_LEVEL_ENCODING_THRESHOLD_DOUBLE) / log(DOC_LEVEL_BASE));
	if (result > DOC_LEVEL_MAX_TF)
		return DOC_LEVEL_MAX_TF;
	else
		return result;
} // end of encodeDocLevelTF(offset)

static inline offset decodeDocLevelTF(offset encoded) {
	if (encoded < DOC_LEVEL_ENCODING_THRESHOLD)
		return encoded;
	double resultDouble = pow(DOC_LEVEL_BASE, encoded - DOC_LEVEL_ENCODING_THRESHOLD);
	return LROUND(DOC_LEVEL_ENCODING_THRESHOLD * resultDouble);
} // end of decodeDocLevelTF(offset)


/** Default file permissions (used whenever index files are created). **/
static const mode_t DEFAULT_FILE_PERMISSIONS = S_IWUSR | S_IRUSR | S_IRGRP;

/** Same as above, but for directories. **/
static const mode_t DEFAULT_DIRECTORY_PERMISSIONS = S_IWUSR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP;


/** The Extent struct is used to represent basic extents (from..to). **/
struct Extent {
	offset from;
	offset to;
};


/**
* This structure is used to reconstruct the original text from an index
* range. Unfortunately, we need this from time to time (statistical feedback,
* display of search results, ...).
**/
struct TokenPositionPair {

	/** Sequence number of the token in the stream. **/
	uint32_t sequenceNumber;

	/** At what position in the file does the token start? **/
	off_t filePosition;

};


struct LongLongPair {
	long long first;
	long long second;
};


void sortArrayOfLongLongPairsByFirst(LongLongPair *array, int n);

void sortArrayOfLongLongPairsBySecond(LongLongPair *array, int n);


/** Sorts the given list of offsets in ascending order, using RadixSort. **/
void sortOffsetsAscending(offset *array, int length);

/** Sorts the given list of offsets in descending order, using RadixSort. **/
void sortOffsetsDescending(offset *array, int length);

/**
 * Same as sortOffsetsAscending, but also removes duplicates. Returns the
 * number of postings, after duplicate-removal.
 **/
int sortOffsetsAscendingAndRemoveDuplicates(offset *array, int length);


/**
 * The following two functions can be used to exchange integer values
 * between two unrelated parts of the program.
 **/
bool getGlobalCounter(const char *name, int64_t *value);
void setGlobalCounter(const char *name, int64_t value);


void ASSERT_ASCENDING(offset *array, int length);

#endif


