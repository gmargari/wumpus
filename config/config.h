/**
 * Copyright (C) 2009 Stefan Buettcher. All rights reserved.
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
 * Compile-time parameters.
 *
 * author: Stefan Buettcher
 * created: 2005-04-23
 * changed: 2011-11-10
 **/


#ifndef __CONFIG__CONFIG_H
#define __CONFIG__CONFIG_H


/** Version ID string of this version of Wumpus. **/
#define WUMPUS_VERSION "2011-11-10"

/**
 * Define whether you want to use 32-bit or 64-bit index offsets. In general,
 * 64 bits are recommended here. If, however, you have a very small collection
 * and want better performance, try if 32-bit offsets help you.
 * Be careful when you switch to 32-bit offsets. It is quite possible that
 * some components of the system will not work properly. Use 64 bits to be
 * on the safe side.
 **/
#define INDEX_OFFSET_BITS 64

/**
 * Maximum length of a token. This value is used pretty much everywhere in the
 * indexing system, from the input streams to the on-disk indices. Make sure
 * this value is of the form 4n-1 for some n. Otherwise, the compiler will
 * complain.
 * Please note that MAX_TOKEN_LENGTH defines the length of the buffer we have
 * to keep in memory for each term in the in-memory index. Thus, large values
 * will result in a huge waste of memory and horrible indexing performance.
 **/
#define MAX_TOKEN_LENGTH 19

/**
 * Depending on whether this is set to true or false, new on-disk inverted
 * files will either be in the old file format (CompactIndex) or in the new
 * one (CompactIndex2).
 **/
#define USE_COMPACTINDEX_2 true

/**
 * Number of terms per dictionary group. A dictionary group is a sequence of
 * in-memory dictionary entries, front-coded to save space.
 **/
#define DICTIONARY_GROUP_SIZE 32

/**
 * Postings for different terms are grouped into blocks in the on-disk indices.
 * For each block, there is a block descriptor in memory. This is the target
 * size of such an on-disk index block. You can expect that the real block
 * size will vary between 50% and 150% of this value.
 **/
#define BYTES_PER_INDEX_BLOCK 65536

/**
 * All postings for the same term are arranged in segments. If there is not
 * enough memory to load all postings into RAM at the same time, we will keep
 * 3 segments in memory. This is the size of each segment. Increasing this
 * value might increase query processing performance and will definitely
 * increase memory consumption. Make sure this value stays in a sensible
 * relation to BYTES_PER_INDEX_BLOCK, i.e.
 * TARGET_SEGMENT_SIZE == BYTES_PER_INDEX_BLOCK / 3 or so.
 **/
#define TARGET_SEGMENT_SIZE 32768

#define MIN_SEGMENT_SIZE (int)(0.65 * TARGET_SEGMENT_SIZE)
#define MAX_SEGMENT_SIZE (int)(1.35 * TARGET_SEGMENT_SIZE)

/**
 * Defines whether posting lists are always completely kept in memory during
 * query processing or whether a caching scheme is used to keep some parts
 * of a posting list in memory, but the majority of the postings on disk.
 **/
#define ALWAYS_LOAD_POSTINGS_INTO_MEMORY 0

/**
 * If this is set to 1, on-disk posting lists are not fetched list-by-list,
 * but index-by-index. This reduces the total disk seek latency.
 * IMPROVED_IO_SCHEDULING can only be used if ALWAYS_LOAD_POSTINGS_INTO_MEMORY
 * is enabled, too.
 **/
#define IMPROVED_IO_SCHEDULING 0

/**
 * The number of slots in the hash table used inside the Lexicon class. For
 * good performance, this has to be a power of 2.
 **/
#define LEXICON_HASHTABLE_SIZE 262144

/**
 * We keep a table of synchronization points between index offsets and the
 * text in the original file. INDEX_TO_TEXT_GRANULARITY defines how frequently
 * we add these synchronization points (1 sync point per N tokens).
 **/
#define INDEX_TO_TEXT_GRANULARITY 4096

/**
 * All files are aligned to N-bit boundaries, i.e. a file always starts
 * at an index address \equiv 0 (mod FILE_GRANULARITY). The exact value is
 * defined here.
 **/
#define FILE_GRANULARITY 16

/**
 * Support for append operations is highly experimental and should not be used.
 * Only turn this knob if you know exactly what it is doing.
 * SUPPORT_APPEND_TAQT realizes the necessary posting list transformation at
 *   query time (TAQT = transformation at query time) by changing the value of
 *   all postings in a posting list.
 * SUPPORT_APPEND_TAIT realizes the transformation at indexing time
 *   (TAIT = transformation at indexing time) by leaving some free address space
 *   at the end of each file and filling new postings in there. As a result,
 *   postings will no longer be strictly increasing, so we need to take care
 *   of that.
 * You can only use one of SUPPORT_APPEND_TAQT, SUPPORT_APPEND_TAIT.
 **/
#define SUPPORT_APPEND_TAQT 0
#define SUPPORT_APPEND_TAIT 0


/**
 * This is the time (in milliseconds) we allow the input stream to wait for an
 * external process to finish input file conversion.
 **/
#define INPUT_CONVERSION_TIMEOUT 5000

/**
 * When tokenizing a string at query time, the result depends on the specific
 * tokenizer used. It can be set with the tokenizer=XX option. The default
 * tokenizer is defined here.
 **/
#define DEFAULT_QUERY_TOKENIZER "text/xml"

/**
 * Indicates whether DOCNO fields in TREC-formatted document collections
 * should receive special treatment (i.e., be cached by a DocIdCache instance).
 * If this is set to 1, then ranked queries can be asked to return document
 * IDs via the [docid] modifier. Otherwise, the doc ID will need to be
 * extracted from the document text.
 **/
#define TREC_DOCNO_CACHING 1

/**
 * Defines the compression type for on-disk indices. Possible values are (among
 * others): COMPRESSION_VBYTE, COMPRESSION_GAMMA, COMPRESSION_NONE.
 * See top of index_compression.h for a complete list of all compression methods
 * supported.
 **/
#define INDEX_COMPRESSION_MODE COMPRESSION_VBYTE

/**
 * These parameters define the strategy used to group postings. When a new term
 * enters the Lexicon, an initial chunk of size LEXICON_INITIAL_CHUNK_SIZE is
 * created. Every time a chunk is full, a new chunk is created. The new chunk's
 * size is N * LEXICON_CHUNK_GROWTH_RATE, where N is the amount of memory
 * occupied by the term's postings so far.
 **/
#define LEXICON_INITIAL_CHUNK_SIZE 15
#define LEXICON_CHUNK_GROWTH_RATE 1.2
#define LEXICON_MAX_CHUNK_SIZE 256


/**
 * Set this to 0 or 1, depending on whether you want to use the allocation
 * debugging mechanisms provided by misc/alloc.cpp.
 **/
#define ALLOC_DEBUG 0

/**
 * Set this to 0 or 1, depending on whether you want to be able to attach gdb to
 * the running program whenever an assertion fails. If set to 1, assert asks the
 * user to press ENTER before it continues execution after a failed assertion.
 **/
#define ASSERT_DEBUG 0


#endif


/**
 * We include an additional header file that performs some sanity checks on the
 * configuration parameters.
 **/
#include "check_config.h"



