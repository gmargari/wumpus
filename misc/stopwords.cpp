/**
 * Implementation of all stopword-related functions.
 *
 * author: Stefan Buettcher
 * created: 2007-11-18
 * changed: 2009-02-01
 **/


#include <set>
#include <string>
#include "alloc.h"
#include "stopwords.h"
#include "language.h"
#include "lockable.h"
#include "stringtokenizer.h"
#include "utils.h"


using namespace std;


static Lockable l;
static bool stopwordsInitialized = false;

static const char* const STOPWORDS_ENGLISH[100] = {
	"", "is", "has", "in", "been", "was", "where", "were", "are", "they", "done",
	"be", "do", "and", "up", "there", "to", "or", "such", "as", "of", "so", "about",
	"the", "a", "an", "by", "that", "this", "these", "which", "for", "on", "he",
	"have", "if", "whether", "what", "who", "will", "it", "their", "his", "had",
	"at", "than", "find", "many", "through", "how", "but", "also", "begin", "them",
	"get", "got", "use", "used", "more", "from", "any", "etc", "gotten", "she",
	"some", "s", "when", "those", "its", "due", "not", "nor", "with", "only",
	"relevant", "document", "documents", "without", "i", "e", "g", "does", "did",
	"t", "no", "could",
	NULL 
};

static const char* const STOPWORDS_GERMAN[100] = {
	"als", "das", "der", "die", "ein", "eine", "haben", "hat", "in", "ist",
	"oder", "sind", "und",
	NULL
};

static set<string> stopwords[MAX_LANGUAGE_ID + 1];


static void initializeStopwordList(const char * const * list, int language) {
	stopwords[language].clear();
	for (int i = 0; list[i] != NULL; i++)
		stopwords[language].insert(list[i]);
} // end of initializeStopwordList(char**, int)


static void initializeStopwordList() {
	LocalLock lock(&l);
	initializeStopwordList(STOPWORDS_ENGLISH, LANGUAGE_ENGLISH);
	initializeStopwordList(STOPWORDS_GERMAN, LANGUAGE_GERMAN);
	stopwordsInitialized = true;
} // end of initializeStopwordList()


bool isStopword(const char *t, int language) {
	if ((language < MIN_LANGUAGE_ID) || (language > MAX_LANGUAGE_ID))
		return false;
	if (!stopwordsInitialized)
		initializeStopwordList();
	char *tmp = duplicateAndTrim(t);
	toLowerCase(tmp);
	bool result = (stopwords[language].find(tmp) != stopwords[language].end());
	free(tmp);
	return result;
} // end of isStopword(char*, int)


void removeStopwordsFromString(char *s, int language) {
	StringTokenizer tok(s, " \t");
	int len = 0;
	for (char *token = tok.nextToken(); token != NULL; token = tok.nextToken())
		if (!isStopword(token, language))
			len += sprintf(&s[len], "%s", token);
} // end of removeStopwordsFromString(char*, int)


