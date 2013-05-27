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
 * The Stemmer class is used to create stemmed forms of words. There are
 * implementations for several languages (see "stemmer.h").
 *
 * author: Stefan Buettcher
 * created: 2004-09-13
 * changed: 2007-11-24
 **/


#include <string.h>
#include "stemmer.h"
#include "../index/index_types.h"
#include "../misc/all.h"
#include "../misc/language.h"


static const char * LOG_ID = "Stemmer";

bool Stemmer::cacheInitialized = false;

StemmingCacheSlot Stemmer::stemmingCache[STEMMING_CACHE_SIZE];

static bool isStemmableChar[256];
static bool isConsonant[256];
static bool stemmerInitialized = false;


/**
 * The SubstitutionRule structure is used to deal with a few irregular word
 * inside the stemmer. Substitution rules can either be applied before or after
 * the stemming.
 **/
typedef struct {

	/** Hash value of the "from" string. **/
	unsigned int fromHashValue;

	/** The "from" string itself, i.e. the original word. **/
	char fromString[MAX_TOKEN_LENGTH + 1];

	/** The "to" string, i.e. the string that the "from" string is transformed to. **/
	char toString[MAX_TOKEN_LENGTH + 1];

	/** "next" pointer for the linked list inside the substitution hash table. **/
	int next;

} SubstitutionRule;


static const int SUBSTITUTION_HASHTABLE_SIZE = 4096;

int postStemmingRules[SUBSTITUTION_HASHTABLE_SIZE];

static const int SUBSTITUION_RULE_COUNT = 100;

SubstitutionRule substitutionRules[SUBSTITUION_RULE_COUNT];


static const char * POSTSTEMMING_IRREGULAR[SUBSTITUION_RULE_COUNT * 2] = {
	"acadian", "acadia",
	"african", "africa",
	"american", "america",
	"analysi", "analys",
	"analyz", "analys",
	"antarctica", "antarct",
	"asian", "asia",
	"australian", "australia",
	"bacteria", "bacterium",
	"behaviour", "behavior",
	"bled", "bleed",
	"built", "build",
	"burnt", "burn",
	"bought", "bui",
	"canadian", "canada",
	"caught", "catch",
	"chines", "china",
	"chose", "choose",
	"colour", "color",
	"criteria", "criterion",
	"eaten", "eat",
	"fallen", "fall",
	"fell", "fall",
	"felt", "feel",
	"fled", "flee",
	"men", "man",
	"women", "woman",
	"children", "child",
	"choic", "choos",
	"dead", "death",
	"deadli", "death",
	"drove", "drive",
	"driven", "drive",
	"drainag", "drain",
	"drank", "drink",
	"drunk", "drink",
	"eastern", "east",
	"failur", "fail",
	"fertilis", "fertil",
	"flew", "fly",
	"flown", "fly",
	"forgot", "forget",
	"forgotten", "forget",
	"french", "franc",
	"germani", "german",
	"healthi", "health",
	"indonesian", "indonesia",
	"influenti", "influenc",
	"injuri", "injur",
	"iranian", "iran",
	"irration", "irrat",
	"iraqi", "iraq",
	"japanes", "japan",
	"learnt", "learn",
	"made", "make",
	"mathemat", "math",
	"mexican", "mexico",
	"northern", "north",
	"norwegian", "norwai",
	"optimis", "optim",
	"persian", "persia",
	"portugues", "portug",
	"reduct", "reduc",
	"registr", "regist",
	"rose", "rise",
	"risen", "rise",
	"russian", "russia",
	"slept", "sleep",
	"spanish", "spain",
	"southern", "south",
	"succeed", "succe",
	"success", "succe",
	"sang", "sing",
	"sung", "sing",
	"sank", "sink",
	"sunk", "sink",
	"swede", "sweden",
	"swedish", "sweden",
	"took", "take",
	"taken", "take",
	"terrorist", "terror",
	"thought", "think",
	"voter", "vote",
	"wealthi", "wealth",
	"western", "west",
	"wrote", "write",
	"written", "write",
	NULL
};


void Stemmer::stemEnglish(char *string) {
	struct SN_env *environment = English1_create_env();
	int len = strlen(string);
	SN_set_current(environment, len, (symbol*)string);
	English1_stem(environment);
	environment->p[environment->l] = 0;
	strcpy(string, (char*)environment->p);
	English1_close_env(environment);

	// see if we can apply postprocessing rules in order to deal with irregular
	// verbs etc. ("flown"->"fly", "women"->"woman", ...)
	unsigned int hv = simpleHashFunction(string);
	int rule = postStemmingRules[hv % SUBSTITUTION_HASHTABLE_SIZE];
	while (rule >= 0) {
		if (substitutionRules[rule].fromHashValue == hv)
			if (strcmp(string, substitutionRules[rule].fromString) == 0) {
				strcpy(string, substitutionRules[rule].toString);
				return;
			}
		rule = substitutionRules[rule].next;
	}
} // end of stemEnglish(char*)


void Stemmer::stemGerman(char *string) {
	struct SN_env *environment;
	int len = strlen(string);
	environment = German_create_env();
	SN_set_current(environment, len, (symbol*)string);
	German_stem(environment);
	environment->p[environment->l] = 0;
	strcpy(string, (char*)environment->p);
	German_close_env(environment);
} // end of stemGerman(char*)


void Stemmer::stem(char *string, int language, bool useCache) {
	unsigned int hashSlot = simpleHashFunction(string) % STEMMING_CACHE_SIZE;

	if (useCache) {
		// use the cache to increase stemming performance
		if (!cacheInitialized) {
			for (int i = 0; i < STEMMING_CACHE_SIZE; i++)
				stemmingCache[i].language = LANGUAGE_NONE;
			cacheInitialized = true;
		}
		// search the cache; maybe we already know the result...
		if (strlen(string) < MAX_CACHED_TOKEN_LENGTH) {
			if (stemmingCache[hashSlot].language == language)
				if (strcmp(stemmingCache[hashSlot].token, string) == 0) {
					strcpy(string, stemmingCache[hashSlot].stem);
					return;
				}
		}
	} // end if (useCache)

	char *originalString = duplicateString(string);
	int outLen = 0;
	StringTokenizer *tok = new StringTokenizer(string, "\t\n .-");

	while (tok->hasNext()) {
		char *word = tok->getNext();

		// we will not process all words; certain rules apply
		bool process;
		if (word[0] == 0)
			process = false;
		else
			process = isStemmable(word);

		if (process) {	
			bool extraordinary = startsWith(word, "<!>");
			if (extraordinary)
				word += 3;
			switch (language) {
				case LANGUAGE_ENGLISH:
					stemEnglish(word);
					break;
				case LANGUAGE_GERMAN:
					stemGerman(word);
					break;
				default:
					word[0] = 0;
					break;
			}
			if (extraordinary) {
				word -= 3;
				if (word[3] == 0)
					word[0] = 0;
			}
		} // end if (process)
		else
			word[0] = 0;

		if ((outLen > 0) && (word[0] != 0))
			string[outLen++] = ' ';
		strcpy(&string[outLen], word);
		outLen += strlen(word);
	}

	delete tok;
	string[outLen] = 0;

	if (useCache) {
		// update the stemming cache
		if ((strlen(string) < MAX_CACHED_TOKEN_LENGTH) && (strlen(originalString) < MAX_CACHED_TOKEN_LENGTH)) {
			stemmingCache[hashSlot].language = language;
			strcpy(stemmingCache[hashSlot].token, originalString);
			strcpy(stemmingCache[hashSlot].stem, string);
		}
	}

	free(originalString);
} // end of stem(char*, int, bool)


void Stemmer::stemWord(char *word, char *stemmed, int language, bool useCache) {
	if ((word[0] == 0) || (!isStemmable(word))) {
		stemmed[0] = 0;
		return;
	}

	int len = strlen(word);

	unsigned int hashSlot;
	if (useCache) {
		// use the cache to increase stemming performance
		hashSlot = simpleHashFunction(word) % STEMMING_CACHE_SIZE;
		if (!cacheInitialized) {
			for (int i = 0; i < STEMMING_CACHE_SIZE; i++)
				stemmingCache[i].language = LANGUAGE_NONE;
			cacheInitialized = true;
		}
		// search the cache; maybe we already know the result...
		if (len < MAX_CACHED_TOKEN_LENGTH) {
			if (stemmingCache[hashSlot].language == language)
				if (strcmp(stemmingCache[hashSlot].token, word) == 0) {
					strcpy(stemmed, stemmingCache[hashSlot].stem);
					return;
				}
		}
	} // end if (useCache)

	strcpy(stemmed, word);
	bool extraordinary = startsWith(stemmed, "<!>");
	if (extraordinary)
		stemmed += 3;
	switch (language) {
		case LANGUAGE_ENGLISH:
			stemEnglish(stemmed);
			break;
		case LANGUAGE_GERMAN:
			stemGerman(stemmed);
			break;
		default:
			stemmed[0] = 0;
			break;
	}
	if (extraordinary) {
		stemmed -= 3;
		if (stemmed[3] == 0)
			stemmed[0] = 0;
	}

	int len2 = strlen(stemmed);
	if (len2 > MAX_TOKEN_LENGTH - 1);
		stemmed[MAX_TOKEN_LENGTH - 1] = 0;

	if (useCache) {
		// update the stemming cache
		if ((len < MAX_CACHED_TOKEN_LENGTH) && (len2 < MAX_CACHED_TOKEN_LENGTH)) {
			stemmingCache[hashSlot].language = language;
			strcpy(stemmingCache[hashSlot].token, word);
			strcpy(stemmingCache[hashSlot].stem, stemmed);
		}
	}
} // end of stemWord(char*, char*, int, bool)


bool Stemmer::stemEquivalent(char *word1, char *word2, int language) {
	if ((strlen(word1) > MAX_TOKEN_LENGTH) || (strlen(word2) > MAX_TOKEN_LENGTH))
		return false;
	char stemmed1[MAX_TOKEN_LENGTH * 2], stemmed2[MAX_TOKEN_LENGTH * 2];
	stemWord(word1, stemmed1, language, false);
	if (stemmed1[0] == 0)
		strcpy(stemmed1, word1);
	stemWord(word2, stemmed2, language, false);
	if (stemmed2[0] == 0)
		strcpy(stemmed2, word2);
	return (strcasecmp(stemmed1, stemmed2) == 0);
} // end of stemEquivalent(char*, char*, int)


bool Stemmer::isStemmable(char *string) {
	if (!stemmerInitialized) {
		// initialize the "isstemmableChar" table that helps us decide whether a given
		// input string can be stemmed
		for (int i = 0; i < 256; i++)
			isStemmableChar[i] = false;
		for (int i = 'A'; i <= 'Z'; i++)
			isStemmableChar[i] = true;
		for (int i = 'a'; i <= 'z'; i++)
			isStemmableChar[i] = true;
		isStemmableChar[(byte)' '] = true;
		for (int i = 0; i < 255; i++) {
			if ((i >= 'A') && (i <= 'Z'))
				isConsonant[i] = true;
			else if ((i >= 'a') && (i <= 'z'))
				isConsonant[i] = true;
			else
				isConsonant[i] = false;
		}
		isConsonant[(byte)'A'] = isConsonant[(byte)'E'] = isConsonant[(byte)'I'] = false;
		isConsonant[(byte)'O'] = isConsonant[(byte)'U'] = isConsonant[(byte)'Y'] = false;
		isConsonant[(byte)'a'] = isConsonant[(byte)'e'] = isConsonant[(byte)'i'] = false;
		isConsonant[(byte)'o'] = isConsonant[(byte)'u'] = isConsonant[(byte)'y'] = false;

		// initialize the post-stemming substitution rules
		int substCnt = 0;
		for (int i = 0; i < SUBSTITUTION_HASHTABLE_SIZE; i++)
			postStemmingRules[i] = -1;
		for (int i = 0; POSTSTEMMING_IRREGULAR[i] != NULL; i += 2) {
			substitutionRules[i/2].fromHashValue =
				simpleHashFunction(POSTSTEMMING_IRREGULAR[i]);
			strcpy(substitutionRules[i/2].fromString, POSTSTEMMING_IRREGULAR[i]);
			strcpy(substitutionRules[i/2].toString, POSTSTEMMING_IRREGULAR[i + 1]);
			unsigned int hashSlot =
				substitutionRules[i/2].fromHashValue % SUBSTITUTION_HASHTABLE_SIZE;
			substitutionRules[i/2].next = postStemmingRules[hashSlot];
			if (postStemmingRules[hashSlot] >= 0) {
				log(LOG_ERROR, LOG_ID, "postStemmingRules hash table is too small!");
				exit(1);
			}
			postStemmingRules[hashSlot] = i/2;
		}
		
		stemmerInitialized = true;
	}
	if (startsWith(string, "<!>"))
		string = &string[3];
	for (int i = 0; string[i] != 0; i++)
		if (!isStemmableChar[(unsigned char)string[i]])
			return false;
	return true;
} // end of isStemmable(char*)





