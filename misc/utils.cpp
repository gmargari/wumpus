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
 * created: 2004-10-11
 * changed: 2011-09-25
 **/


#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "assert.h"
#include "utils.h"
#include "alloc.h"


typedef unsigned char byte;


char *chop(char *s) {
	if (s == NULL)
		return NULL;
	while ((*s > 0) && (*s <= ' '))
		s = &s[1];
	s = duplicateString(s);
	int len = strlen(s);
	while ((len > 1) && (s[len - 1] > 0) && (s[len - 1] <= ' '))
		len = len - 1;
	s[len] = 0;
	return s;
} // end of chop(char*)


void collapsePath(char *path) {
	char *slashSlash = strstr(path, "//");
	while (slashSlash != NULL) {
		for (int i = 1; slashSlash[i] != 0; i++)
			slashSlash[i] = slashSlash[i + 1];
		slashSlash = strstr(path, "//");
	}
	char *slashDotSlash = strstr(path, "/./");
	while (slashDotSlash != NULL) {
		for (int i = 2; slashDotSlash[i] != 0; i++)
			slashDotSlash[i - 1] = slashDotSlash[i + 1];
		slashDotSlash = strstr(path, "/./");
	}
	char *slashDotDotSlash = strstr(path, "/../");
	while (slashDotDotSlash != NULL) {
		if (slashDotDotSlash == path) {
			memmove(path, &path[3], strlen(path) - 3);
			slashDotDotSlash = strstr(path, "/../");
		}
		int pos = ((int)(slashDotSlash - path)) + 3;
		int found = -1;
		for (int i = pos - 4; i >= 0; i--)
			if (path[i] == '/') {
				found = i;
				break;
			}
		if (found < 0)
			break;
		strcpy(&path[found], &path[pos]);
		slashDotDotSlash = strstr(path, "/../");
	}
	int len = strlen(path);
	if (strcmp(&path[len - 3], "/..") == 0) {
		len = len - 3;
		path[len] = 0;
		if (len > 1) {
			while ((len > 1) && (path[len] != '/'))
				len--;
			path[len] = 0;
		}
	}
	else if (strcmp(&path[len - 2], "/.") == 0)
		path[len - 2] = 0;
	else if ((len > 2) && (path[len - 1] == '/'))
		path[len - 1] = 0;
	if (path[0] == 0)
		strcpy(path, "/");
} // end of collapsePath(char*)


void randomTempFileName(char *pattern) {
	static int counter = 0;
	counter++;
	if (counter % 1024 == 1) {
		int randomFile = open("/dev/urandom", O_RDONLY);
		if (randomFile >= 0) {
			unsigned int seed;
			if (read(randomFile, &seed, sizeof(seed)) == sizeof(seed))
				srandom(seed);
			close(randomFile);
		}
	}
	for (int i = 0; pattern[i] != 0; i++)
		if (pattern[i] == 'X') {
			int value = random() % 36;
			if (value < 10)
				pattern[i] = (char)('0' + value);
			else {
				value -= 10;
				pattern[i] = (char)('a' + value);
			}
		}
} // end of randomTempFileName(char*)


void waitMilliSeconds(int ms) {
	if (ms <= 1)
		return;
	struct timespec t1, t2;
	t1.tv_sec = (ms / 1000);
	t1.tv_nsec = (ms % 1000) * 1000000;
	int result = nanosleep(&t1, &t2);
	if (result < 0) {
		t1 = t2;
		nanosleep(&t1, &t2);
	}
} // end of waitMilliSeconds(int)


int currentTimeMillis() {
	struct timeval currentTime;
	int result = gettimeofday(&currentTime, NULL);
	assert(result == 0);
	int seconds = currentTime.tv_sec;
	int microseconds = currentTime.tv_usec;
	return (seconds * 1000) + (microseconds / 1000);
} // end of currentTimeMillis()


double getCurrentTime() {
	struct timeval currentTime;
	int result = gettimeofday(&currentTime, NULL);
	assert(result == 0);
	return currentTime.tv_sec + 1E-6 * currentTime.tv_usec;
} // end of getCurrentTime()


unsigned int simpleHashFunction(const char *string) {
	unsigned int result = 0;
	for (int i = 0; string[i] != 0; i++)
		result = result * 127 + (byte)string[i];
	return result;
} // end of simpleHashFunction(char*)


double stirling(double n) {
	if (n < 1.0)
		return 1.0;
	else
		return pow(n / M_E, n) * sqrt(2 * M_PI * n) * (1 + 12/n);
} // end of stirling(double)


char *duplicateString3(const char *s, const char *file, int line) {
	if (s == NULL)
		return NULL;
#if ALLOC_DEBUG
	char *result = (char*)debugMalloc(strlen(s) + 1, file, line);
#else
	char *result = (char*)malloc(strlen(s) + 1);
#endif
	strcpy(result, s);
	return result;
} // end of duplicateString3(char*, char*, int)


void toLowerCase(char *s) {
	for (int i = 0; s[i] != 0; i++)
		if ((s[i] >= 'A') && (s[i] <= 'Z'))
			s[i] |= 32;
} // end of toLowerCase(char*)


void trimString(char *s) {
	int len = strlen(s);
	char *p = s;
	while ((*p > 0) && (*p <= 32)) {
		p++;
		len--;
	}
	if (p != s)
		memmove(s, p, len);
	while ((len > 0) && (s[len - 1] > 0) && (s[len - 1] <= 32))
		len--;
	s[len] = 0;
} // end of trimString(char*)


char *duplicateAndTrim(const char *s) {
	char *result = duplicateString(s);
	trimString(result);
	return result;
} // end of duplicateAndTrim(char*)


char *concatenateStrings(const char *s1, const char *s2) {
	int len1 = strlen(s1);
	int len2 = strlen(s2);
	char *result = (char*)malloc(len1 + len2 + 1);
	strcpy(result, s1);
	strcpy(&result[len1], s2);
	return result;
} // end of concatenateStrings(char*, char*)


char *concatenateStrings(const char *s1, const char *s2, const char *s3) {
	int len1 = strlen(s1);
	int len2 = strlen(s2);
	int len3 = strlen(s3);
	char *result = (char*)malloc(len1 + len2 + len3 + 1);
	strcpy(result, s1);
	strcpy(&result[len1], s2);
	strcpy(&result[len1 + len2], s3);
	return result;
} // end of concatenateStrings(char*, char*, char*)


char *concatenateStringsAndFree(char *s1, char *s2) {
	char *result = concatenateStrings(s1, s2);
	free(s1);
	free(s2);
	return result;
} // end of concatenateStringsAndFree(char*, char*)


char *getSubstring(const char *s, int start, int end) {
	int sLen = strlen(s);
	if (start >= sLen)
		return duplicateString("");
	s = &s[start];
	end -= start;
	sLen -= start;
	char *result = duplicateString(s);
	if (sLen >= end)
		result[end] = 0;
	return result;
} // end of getSubstring(char*, int, int)


bool startsWith(const char *longString, const char *shortString, bool caseSensitive) {
	if ((longString == NULL) || (shortString == NULL))
		return false;
	int len = strlen(shortString);
	if (caseSensitive)
		return (strncmp(longString, shortString, len) == 0);
	else
		return (strncasecmp(longString, shortString, len) == 0);
} // end of startsWith(char*, char*, bool)


bool endsWith(const char *longString, const char *shortString, bool caseSensitive) {
	if ((longString == NULL) || (shortString == NULL))
		return false;
	return endsWith(longString, strlen(longString),
	                shortString, strlen(shortString), caseSensitive);
} // end of endsWith(char*, char*, bool)


bool endsWith(const char *longString, int longLength,
              const char *shortString, int shortLength, bool caseSensitive) {
	if ((longString == NULL) || (shortString == NULL))
		return false;
	if (shortLength > longLength)
		return false;
	longString = &longString[longLength - shortLength];
	if (caseSensitive)
		return (strncmp(longString, shortString, shortLength) == 0);
	else
		return (strncasecmp(longString, shortString, shortLength) == 0);
} // end of endsWith(char*, int, char*, int, bool)


char *evaluateRelativePathName(const char *dir, const char *file) {
	int dirLen = strlen(dir);
	if (file[0] == '/')
		file++;
	int fileLen = strlen(file);
	int totalLen = dirLen + fileLen + 4;
	char *result = (char*)malloc(totalLen);
	sprintf(result, "%s%s%s", dir, (dir[dirLen - 1] == '/' ? "" : "/"), file);
	collapsePath(result);
	return result;
} // end of evaluateRelativePathName(char*, char*)


char *printOffset(int64_t o, char *where) {
	if (where == NULL)
		where = (char*)malloc(20);
	int upper = o / 1000000000;
	int lower = o % 1000000000;
	if (upper > 0)
		sprintf(where, "%d%09d", upper, lower);
	else
		sprintf(where, "%d", lower);
	return where;
} // end of printOffset(int64_t, char*)


void normalizeURL(char *url) {
	int len = strlen(url);
	if (strncasecmp(url, "http://", 7) == 0) {
		memmove(url, &url[7], len - 6);
		len -= 7;
	}
	char *firstSlash = strchr(url, '/');
	char *lastSlash = strrchr(url, '/');
	if (firstSlash == NULL)
		firstSlash = &url[len];
	while (url != firstSlash) {
		if ((*url >= 'A') && (*url <= 'Z'))
			*url += 32;
		url++;
	}
	if (lastSlash != NULL) {
		if ((strcasecmp(lastSlash, "/") == 0) ||
		    (strcasecmp(lastSlash, "/index.html") == 0) ||
		    (strcasecmp(lastSlash, "/index.htm") == 0) ||
		    (strcasecmp(lastSlash, "/default.html") == 0) ||
		    (strcasecmp(lastSlash, "/default.htm") == 0))
			*lastSlash = 0;
	}
	firstSlash = strchr(url, '/');
	if (firstSlash != NULL)
		collapsePath(firstSlash);
} // normalizeURL(char*)


char *normalizeString(char *s) {
	for (int i = 0; s[i] != 0; i++) {
		char c = s[i];
		if ((c < 0) || ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')))
			s[i] = c;
		else if ((c >= 'A') && (c <= 'Z'))
			s[i] = tolower(c);
		else
			s[i] = ' ';
	}
	StringTokenizer tok(s, " ");
	int len = 0;
	for (char *token = tok.nextToken(); token != NULL; token = tok.nextToken()) {
		if (token[0] == 0)
			continue;
		if (len == 0)
			len = sprintf(s, "%s", token);
		else
			len += sprintf(&s[len], " %s", token);
	}
	s[len] = 0;
	return s;
} // end of normalizeString(char*)


void normalizeString(std::string *s) {
	char *tmp = duplicateString(s->c_str());
	normalizeString(tmp);
	*s = tmp;
	free(tmp);
} // end of normalizeString(string*)


char *evaluateRelativeURL(const char *base, const char *link) {
	if (strncasecmp(link, "http://", 7) == 0)
		return duplicateString(link);

	char *result = (char*)malloc(strlen(base) + strlen(link) + 4);
	strcpy(result, base);
	char *p = result;
	if (strncasecmp(p, "http://", 7) == 0)
		p = &p[7];

	// find beginning of path name (after host) and remove everything
	// after the last slash; after this operation, the "firstSlash" string
	// will represent the path and end with a slash
	char *firstSlash, *lastSlash;
	firstSlash = strchr(p, '/');
	if (firstSlash == NULL) {
		int len = strlen(p);
		firstSlash = lastSlash = &p[len];
		strcpy(firstSlash, "/");
	}
	else {
		lastSlash = strrchr(firstSlash, '/');
		assert(lastSlash != NULL);
		lastSlash[1] = 0;
	}

	// append link to path (or replace path in case link starts with '/') and
	// collapse (remove ".." etc.)
	if (link[0] == '/')
		strcpy(firstSlash, link);
	else
		strcat(firstSlash, link);
	collapsePath(firstSlash);

	return result;
} // evaluateRelativeURL(char*, char*)


char *extractLastComponent(const char *filePath, bool copy) {
	char *ptr = const_cast<char*>(strrchr(filePath, '/'));
	ptr = (ptr == NULL ? const_cast<char*>(filePath) : &ptr[1]);
	return (copy ? duplicateString(ptr) : ptr);
} // end of extractLastComponent(char*, bool)


void printOffset(int64_t o, FILE *stream) {
	char temp[32];
	printOffset(o, temp);
	printf("%s", temp);
} // end of printOffset(FILE*)


static bool matchesPattern(const char *string, int stringPos, const char *pattern, int patternPos) {

matchesPattern_START:

	// match all non-wildcard characters, as far as possible
	while ((pattern[patternPos] != '?') && (pattern[patternPos] != '*') && (pattern[patternPos] != 0)) {
		if (string[stringPos] != pattern[patternPos])
			return false;
		stringPos++;
		patternPos++;
	}

	// if string is over, either return true or false, depending on whether
	// the pattern equals "*" or not
	if (string[stringPos] == 0) {
		while (pattern[patternPos] != 0) {
			if (pattern[patternPos] != '*')
				return false;
			patternPos++;
		}
		return true;
	}

	// if we still have string, but no more pattern, return false
	if (pattern[patternPos] == 0)
		return false;

	// if current pattern character is '?', match that against the current character
	// of the string and go back to START
	if (pattern[patternPos] == '?') {
		patternPos++;
		stringPos++;
		goto matchesPattern_START;
	}

	// if we get here, this means the current pattern character is '*'
	assert(pattern[patternPos] == '*');

	// contract consecutive '*' instances
	while (pattern[patternPos + 1] == '*')
		patternPos++;
	patternPos++;

	if (pattern[patternPos] == 0)
		return true;

	// try all possibilities of matching this '*' with the remainder of "string"
	while (string[stringPos] != 0) {
		if (matchesPattern(string, stringPos, pattern, patternPos))
			return true;
		stringPos++;
	}

	return false;
} // end of matchesPattern(char*, int, char*, int)


bool matchesPattern(const char *string, const char *pattern) {
	// refuse to process anything with more than 3 "*" in it
	int cnt = 0;
	for (int i = 0; pattern[i] != 0; i++)
		if (pattern[i] == '*')
			if (pattern[i + 1] != '*')
				cnt++;
	if (pow(strlen(string), cnt) > 100000)
		return false;
	else
		return matchesPattern(string, 0, pattern, 0);
} // end of matchesPattern(char*, char*)


int doubleComparator(const void *a, const void *b) {
	double *x = (double*)a;
	double *y = (double*)b;
	if (*x > *y)
		return -1;
	else if (*x < *y)
		return +1;
	else
		return 0;
} // end of doubleComparator(const void*, const void*)


bool isNumber(const char *s) {
	if (*s == '-')
		s++;
	do {
		if ((*s < '0') || (*s > '9'))
			return false;
		s++;
	} while (*s != 0);
	return true;
} // end of isNumber(char*)


int getHammingWeight(unsigned int n) {
	int result = 0;
	while (n != 0) {
		result += (n & 1);
		n >>= 1;
	}
	return result;
} // end of getHammingWeight(unsigned int)


bool compareNumbers(double a, double b, const char *comparator) {
	static const double EPSILON = 0.000001;
	char c[32];
	if (strlen(comparator) > 30)
		return false;
	if (sscanf(comparator, "%s", c) == 0)
		return false;
	if ((strcmp(c, "=") == 0) || (strcmp(c, "==") == 0))
		return (fabs(a - b) < EPSILON);
	else if (strcmp(c, ">=") == 0)
		return (a > b - EPSILON);
	else if (strcmp(c, ">") == 0)
		return (a > b + EPSILON);
	else if (strcmp(c, "<=") == 0)
		return (a < b + EPSILON);
	else if (strcmp(c, "<") == 0)
		return (a < b - EPSILON);
	else
		return false;
} // end of compareNumbers(double, double, char*)


double logFactorial(int n) {
	double result = 0.0;
	for (int i = 2; i <= n; i++)
		result += log(i);
	return result;
} // end of logFactorial(int)


double n_choose_k(int n, int k) {
	if ((k < 0) || (k > n))
		return 0;
	double result = logFactorial(n) - logFactorial(k) - logFactorial(n - k);
	return exp(result);
} // end of n_choose_k(int, int)


bool fileExists(const char *fileName) {
	struct stat buf;
	int status = stat(fileName, &buf);
	return ((status == 0) && (S_ISREG(buf.st_mode)));
} // end of fileExists(char*)


int64_t getFileSize(const char *fileName) {
	struct stat buf;
	int status = stat(fileName, &buf);
	if ((status != 0) || (!S_ISREG(buf.st_mode)))
		return -1;
	else
		return buf.st_size;
} // end of getFileSize(char*)


void replaceChar(char *string, char oldChar, char newChar, bool replaceAll) {
	while (*string != 0) {
		if (*string == oldChar) {
			*string = newChar;
			if (!replaceAll)
				return;
		}
		string++;
	}
} // end of replaceChar(char*, char, char, bool)


void getNextNonCommentLine(FILE *f, char *buffer, int bufferSize) {
	buffer[0] = '#';
	while (buffer[0] == '#') {
		if (fgets(buffer, bufferSize, f) == NULL) {
			buffer[0] = 0;
			return;
		}
	}
} // end of getNextNonCommentLine(FILE*, char*, int)



