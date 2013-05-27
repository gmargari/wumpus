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
 * This file is used to check the values of the configuration parameters
 * specified in "config.h". I keep the sanity checks in a different file
 * because that makes it more difficult to accidentally change them.
 *
 * author: Stefan Buettcher
 * created: 2005-07-25
 * changed: 2007-03-01
 **/


#ifndef __CONFIG__CHECK_CONFIG_H
#define __CONFIG__CHECK_CONFIG_H


// Check whether INDEX_OFFSET_BITS is either 32 or 64.
#if (INDEX_OFFSET_BITS != 32) && (INDEX_OFFSET_BITS != 64)
  #error Illegal value for configuration parameter INDEX_OFFSET_BITS.
  #error Legal values are: 32, 64.
  #undef INDEX_OFFSET_BITS
  #define INDEX_OFFSET_BITS 32
#endif


// Check whether MAX_TOKEN_LENGTH is \equiv 3 (modulo 4).
#if ((MAX_TOKEN_LENGTH % 4 != 3) || (MAX_TOKEN_LENGTH < 0))
  #error Illegal value for configuration parameter MAX_TOKEN_LENGTH.
  #error Legal values are: 3, 7, 11, 15, 19, 23, ...
  #undef MAX_TOKEN_LENGTH
  #define MAX_TOKEN_LENGTH 15
#endif


// Check whether MAX_TOKEN_LENGTH is small enough in order to allow for efficient
// indexing.
#if (MAX_TOKEN_LENGTH > 99)
	#error Illegal value for configuration parameter MAX_TOKEN_LENGTH.
	#error Please choose MAX_TOKEN_LENGTH < 100.
#endif


// Check whether the size of the lexicon's hash table is a power of 2.
#if (LEXICON_HASHTABLE_SIZE != (1 << 16)) && (LEXICON_HASHTABLE_SIZE != (1 << 17)) && \
		(LEXICON_HASHTABLE_SIZE != (1 << 18)) && (LEXICON_HASHTABLE_SIZE != (1 << 19)) && \
		(LEXICON_HASHTABLE_SIZE != (1 << 20)) && (LEXICON_HASHTABLE_SIZE != (1 << 21))
	#warning Non-standard size of in-memory hash table.
	#warning For optimal performance, choose a power of 2.
#endif


// If IMPROVED_IO_SCHEDULING is set, we need to make sure that
// ALWAYS_LOAD_POSTINGS_INTO_MEMORY is set as well. Otherwise, no sense make.
#if IMPROVED_IO_SCHEDULING
	#if (ALWAYS_LOAD_POSTINGS_INTO_MEMORY != 1)
		#error IMPROVED_IO_SCHEDULING only works with ALWAYS_LOAD_POSTINGS_INTO_MEMORY.
	#endif
#endif


#if (SUPPORT_APPEND_TAQT && SUPPORT_APPEND_TAIT)
	#error You can only use one at a time: SUPPORT_APPEND_TAQT or SUPPORT_APPEND_TAIT
#endif


#endif


