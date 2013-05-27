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
 * This header file contains the class definition of the Stemmer class.
 *
 * author: Stefan Buettcher
 * created: 2004-09-13
 * changed: 2007-11-18
 **/


#ifndef __STEMMING__STEMMER_H
#define __STEMMING__STEMMER_H


#include "api.h"
#include "english1.h"
#include "english2.h"
#include "german.h"


/** The stemming cache will only contain tokens with length smaller than this. **/
#define MAX_CACHED_TOKEN_LENGTH 16


/**
 * The StemmingCacheSlot structure is used to keep track of recent stemming
 * results, which are used to speed up the indexing process.
 **/
typedef struct {

	/** What language are we talking about? **/
	int language;

	/** The original token. **/
	char token[MAX_CACHED_TOKEN_LENGTH];

	/** Its stemmed form. **/
	char stem[MAX_CACHED_TOKEN_LENGTH];

} StemmingCacheSlot;


class Stemmer {

public:

	static const int STEMMING_CACHE_SIZE = 1024;

private:

	static bool cacheInitialized;

	static StemmingCacheSlot stemmingCache[STEMMING_CACHE_SIZE];

public:

	/**
	 * Stems the word found in the string "string" using the built-in (porter)
	 * rules for English stemming. "string" may only contain one word. Its
	 * content is overwritten by the method.
	 **/
	static void stemEnglish(char *string);

	/**
	 * Uses stemming algorithm for German words to stem the word found in
	 * the string given by "string". There may only be one word in "string".
	 * "string" is overwritten.
	 **/
	static void stemGerman(char *string);

	/**
	 * Stems the word(s) contained in the string "string" using language-specific
	 * rules. The language of the words is specified by "language". A cache containing
	 * earlier stemming results may be used. However, please be aware that the
	 * stemming cache is not thread-safe.
	 **/
	static void stem(char *string, int language, bool useCache);

	/** Same as above, but faster. Only for single-word strings. **/
	static void stemWord(char *word, char *stemmed, int language, bool useCache);

	/** Returns true if the two given words are stem-equivalent in the given language. **/
	static bool stemEquivalent(char *word1, char *word2, int language);

	/** Returns true iff "string" is something that can actually be stemmed. **/
	static bool isStemmable(char *string);

	static int getHashValue(char *string, int language);

	static unsigned int getHashValue(char *string);

}; // end of class Stemmer

#endif
	
 
