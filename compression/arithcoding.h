#ifndef __COMPRESSION__ARITHCODING_H
#define __COMPRESSION__ARITHCODING_H


#include "../index/index_types.h"


/**
 * Encodes a given list of integers, containing "listLength" elements, using
 * arithmetic coding. If "semiStatic" is set to true, the function will
 * perform a frequency count and prepend statistics to the output instead
 * of using adaptive encoding. Semi-static encoding only makes sense if the
 * number of distinct symbols is much smaller than the number of elements in
 * the list. The number of bytes consumed by the encoded representation will
 * be stored in "byteLength". Memory has to be freed by the caller.
 **/
byte * arithEncode(int *uncompressed, int listLength, bool semiStatic, int *byteLength);


/**
 * Counterpart to arithEncode. Takes a pointer to a list of integers encoded
 * using arithEncode and returns a pointer to the decompressed list. The number
 * of elements in the list will be stored in "listLength". Memory has to be
 * freed by the caller.
 **/
int * arithDecode(byte *compressed, int *listLength);


#endif


