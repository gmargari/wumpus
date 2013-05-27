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
 * A bunch of useful functions.
 *
 * author: Stefan Buettcher
 * created: 2004-10-11
 * changed: 2009-02-01
 **/


#ifndef __MISC__UTILS_H
#define __MISC__UTILS_H


#include <string>
#include <stdio.h>
#include <sys/types.h>
#include "macros.h"


#ifndef MAX
	#define MAX(a, b) (a > b ? a : b)
#endif
#ifndef MIN
	#define MIN(a, b) (a < b ? a : b)
#endif

#define IS_WILDCARD_CHAR(c) ((c == '?') || (c == '*'))

// common macro used by all classes to initialize their semaphores
#ifdef __APPLE__
// MacOS doesn't give us posix semaphores for some reason.
#define SEM_INIT(SEM, CNT)
#else
#define SEM_INIT(SEM, CNT) if (sem_init(&SEM, 0, CNT) < 0) { \
	log(LOG_ERROR, LOG_ID, "Failed to initialize semaphore: " __STRING(SEM) " (" __FILE__ ")"); \
	assert(false); \
}
#endif

#define log2(x) (log(x) / log(2))


static const int SECONDS_PER_DAY = 24 * 3600;

static const int MILLISECONDS_PER_DAY = 24 * 3600 * 1000;


/**
 * Takes a fully qualified path name ("path") and removes all non-canonical
 * parts from it ("//", "/./", "/../").
 **/
void collapsePath(char *path);

/**
 * Takes a filename pattern of the form ...XXXXXX... and converts all
 * occurrences of X into random characters. I am using this to get rid
 * of the warning that appears if I call mktemp.
 **/
void randomTempFileName(char *pattern);

/** Suspends the execution of the thread for "ms" milliseconds. **/
void waitMilliSeconds(int ms);

/**
 * Returns the current time in milliseconds. Please note that this is only
 * intra-day time. But this is good enough for what we want to do with it.
 **/
int currentTimeMillis();

/**
 * Same as above, but using seconds as the basic unit of measurement. Resolution
 * is around 1 microsecond.
 **/
double getCurrentTime();

/**
 * Very stupid hash function. Returns an unsigned integer value -- the hash
 * value of the string "string".
 **/
unsigned int simpleHashFunction(const char *string);

/** Returns the factorial n!, using Stirling's formula. **/
double stirling(double n);

/** Creates a copy of the string "s" and returns a pointer to the new string. **/
#define duplicateString(s) duplicateString3(s, __FILE__, __LINE__)
char *duplicateString3(const char *s, const char *file, int line);

void toLowerCase(char *s);

/** Removes heading and trailing whitespace characters. **/
void trimString(char *s);

/** Creates a duplicate of "s", heading and trailing spaces removed. **/
char *duplicateAndTrim(const char *s);

/**
 * Takes two strings and creates a third string that is the concatenation of
 * the two input strings. Memory has to be freed by the caller.
 **/
char *concatenateStrings(const char *s1, const char *s2);

/** Same as above, but for 3 input strings. **/
char *concatenateStrings(const char *s1, const char *s2, const char *s3);

/** Same as above, but frees the input strings "s1" and "s2". **/
char *concatenateStringsAndFree(char *s1, char *s2);

/**
 * Returns the substring of "s" starting at "start" (counted from 0) and
 * ending just before "end". Memory has to be freed by the caller.
 **/
char *getSubstring(const char *s, int start, int end);

/** Returns true iff "shortString" is a prefix of "longString". **/
bool startsWith(const char *longString, const char *shortString,
                bool caseSensitive = true);

bool endsWith(const char *longString, const char *shortString,
              bool caseSensitive = true);

bool endsWith(const char *longString, int longLength,
              const char *shortString, int shortLength,
							bool caseSensitive = true);

/**
 * Creates a new path by interpreting "file" as a relative path expression
 * and applying it to "dir". Memory has to be freed by the caller.
 **/
char *evaluateRelativePathName(const char *dir, const char *file);

/**
 * Same as evaluateRelativePathName, but for URLs. Semantics are a little
 * bit different, because base does not need to be a directory.
 **/
char *evaluateRelativeURL(const char *base, const char *link);

/**
 * Returns the last component (file name) of the given file path. If "copy" is
 * set to true, the pointer returned references newly allocated memory and has
 * to be freed by the caller. Otherwise, it points into "filePath" and must not
 * be tampered with.
 **/
char *extractLastComponent(const char *filePath, bool copy);

/** Takes a URL and transforms it into normalized form for easier comparison. **/
void normalizeURL(char *url);

/**
 * Takes a string, replaces all punctuation characters by whitespace, and
 * lowercases the rest. Replaces multiple consecutive whitespace characters by
 * a single one.
 **/
char *normalizeString(char *s);
void normalizeString(std::string *s);

/**
 * Prints the offset given by "o" to the memory referenced by "where" and returns
 * a pointer to "where". If "where == NULL", new memory is allocated first and a
 * pointer to that memory is returned.
 **/
char *printOffset(int64_t o, char *where);

/** Same as above, but prints to a stream. **/
void printOffset(int64_t o, FILE *stream);

/**
 * Returns a string that has leading and trailing whitespaces removed. Memory
 * has to be freed by the caller.
 **/
char *chop(char *s);

/**
 * Returns true iff the string "string" matches the pattern "pattern". The pattern
 * is an arbitrary wildcards string, i.e. with "?" and "*".
 **/
bool matchesPattern(const char *string, const char *pattern);

/** Comparator for qsort and double arrays. Sorts in descending order. **/
int doubleComparator(const void *a, const void *b);

/** Returns true iff the string "s" is an integer number. **/
bool isNumber(const char *s);

/** Replaces one or all occurrences of "oldChar" within "string" by "newChar". **/
void replaceChar(char *string, char oldChar, char newChar, bool replaceAll);

/**
 * Returns true iff the condition
 *   "a comparator b"
 * is met. The comparator can be one of: <, <=, =, >=, >.
 **/
bool compareNumbers(double a, double b, const char *comparator);

/** Returns the number of "1" bits in the given integer. **/
int getHammingWeight(unsigned int n);

/** Returns ln(n!). Used by n_choose_k, for instance. **/
double logFactorial(int n);

/** Returns (n choose k). **/
double n_choose_k(int n, int k);

/** Returns true iff the given file exists. **/
bool fileExists(const char *fileName);

/** Returns the size of the given file. -1 if no such file exists. **/
int64_t getFileSize(const char *fileName);

void getNextNonCommentLine(FILE *f, char *buffer, int bufferSize);

#endif


