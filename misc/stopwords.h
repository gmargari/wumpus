/**
 * Declaration of stopword-related functions.
 *
 * author: Stefan Buettcher
 * created: 2007-11-18
 * changed: 2007-11-18
 **/


#ifndef __MISC__STOPWORDS_H
#define __MISC__STOPWORDS_H


/**
 * Returns true iff the given term is a stopword in the given 
 **/
bool isStopword(const char *t, int language);

/**
 * Takes a string containing a sequence of tokens, delimited by whitespace
 * characters, and removes all stopword tokens from the sequence.
 **/
void removeStopwordsFromString(char *s, int language);


#endif
