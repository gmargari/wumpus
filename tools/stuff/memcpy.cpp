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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const int ARRAY_SIZE = 32768;


static const void memcpy2(char *dst, char *src, int count) {
	int cnt = (count >> 1);
	int rem = (cnt & 3);
	asm volatile (
		"rep movsw"  // move everything from *src to *dst
		:            // no output variables
		:"D"(dst), "S"(src), "c"(cnt)
		// load dst into %edi, src into %esi, cnt into %ecx
		:"memory"
	);
	for (int i = count - 1 - rem; i < count; i++)
		dst[i] = src[i];
} // end of memcpy2(char*, char*, int)


int main() {
	char *array1 = (char*)malloc(ARRAY_SIZE);
	char *array2 = (char*)malloc(ARRAY_SIZE);
	for (int i = 0; i < 10000; i++) {
		memcpy2(array2, array1, ARRAY_SIZE);
		memcpy2(array1, array2, ARRAY_SIZE);
	}
	return 0;
}



