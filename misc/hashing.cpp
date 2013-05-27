/**
 * author: Stefan Buettcher
 * created: 2005-12-23
 * changed: 2005-12-23
 **/


#include <stdio.h>
#include <string.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include "hashing.h"
#include "all.h"


uint64_t getUnsecureHashValue(char *string) {
	int len = strlen(string);
	return getUnsecureHashValue((unsigned char*)string, len);
} // end of getUnsecureHashValue(char*)


uint64_t getUnsecureHashValue(unsigned char *buffer, int len) {
	uint64_t result = 0;
	for (int i = 0; i < len; i++)
		result = (result * 127) + buffer[i];
	return result;
} // end of getUnsecureHashValue(unsigned char*, int)


uint64_t getHashValue_SHA1(void *buffer, int len) {
	unsigned char md[SHA_DIGEST_LENGTH];
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, buffer, len);
	SHA1_Final(md, &ctx);
	return getUnsecureHashValue(md, sizeof(md));
} // end of getHashValue_SHA1(void*, int)


uint64_t getHashValue_MD5(void *buffer, int len) {
	unsigned char md[MD5_DIGEST_LENGTH];
	MD5_CTX ctx;
	MD5_Init(&ctx);
	MD5_Update(&ctx, buffer, len);
	MD5_Final(md, &ctx);
	return getUnsecureHashValue(md, sizeof(md));
} // end of getHashValue_MD5(void*, int)



