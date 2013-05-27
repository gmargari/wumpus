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
 * Some general constants and stuff for my Terabyte implementation.
 *
 * author: Stefan Buettcher
 * created: 2005-06-03
 * changed: 2006-03-26
 **/


#ifndef __TERABYTE__TERABYTE_H
#define __TERABYTE__TERABYTE_H


#include "../index/index_types.h"
#include "../misc/utils.h"
#include <stdlib.h>


/**
 * When creating an on-disk index, only add those terms from the lexicon which
 * appear in at least DOCUMENT_COUNT_THRESHOLD documents.
 **/
static const int DOCUMENT_COUNT_THRESHOLD = 2;


/**
 * This is used to encode the length of the original posting list in the restricted
 * version that is found in the small (in-memory?) index. We append an additional
 * posting (DOCUMENT_COUNT_OFFSET + ORIGINAL_LENGTH) at the end of the restricted
 * list.
 **/
static const offset DOCUMENT_COUNT_OFFSET = 1000000000000LL;


/**
 * Tells us whether we spawn multiple threads to fetch the posting lists
 * in parallel or whether we have to do it sequentially.
 **/
#define FETCH_POSTINGS_IN_PARALLEL 1


/**
 * In the impact-restricted index, we store the average impact of a given term,
 * that is the sum of this term's impact in all documents over the number of documents
 * in the corpus. Since the average impact has to be encoded as an integer in the
 * index, we multiply it by this number before we transform it to an integer value.
 **/
static const double IMPACT_INTEGER_SCALING = 10000.0;


#endif


