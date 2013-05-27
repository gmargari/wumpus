/**
 * Wrapper functions for SHA1 and MD5, returning 64-bit hash values.
 *
 * author: Stefan Buettcher
 * created: 2005-12-23
 * changed: 2006-07-16
 **/


#ifndef __MISC__HASHING_H
#define __MISC__HASHING_H


#include "all.h"


uint64_t getUnsecureHashValue(char *string);

uint64_t getUnsecureHashValue(unsigned char *buffer, int len);

uint64_t getHashValue_SHA1(void *buffer, int len);

uint64_t getHashValue_MD5(void *buffer, int len);


#endif

