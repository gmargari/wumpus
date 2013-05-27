/**
 * author: Stefan Buettcher
 * created: 2006-07-16
 * changed: 2006-07-16
 **/


#ifndef __MISC_DOCUMENT_ANALYZER_H
#define __MISC_DOCUMENT_ANALYZER_H


#include <map>
#include <string>
#include <vector>
#include "../index/index_types.h"


class Index;

using namespace std;


class DocumentAnalyzer {

public:

	/**
	 * Stores the TREC document ID and the document's URL in the given buffers.
	 * Returns true if successful, false if components could not be found.
	 * "base" will either contain the URL or, if present, the value of the
	 " BASE HREF thingy.
	 **/
	static bool analyzeTRECHeader(
			string document, string *docID, string *url, string *base);

	/** Same as above, but used the given Index to fetch the document text. **/
	static bool analyzeTRECHeader(Index *index,
			offset documentStart, offset documentEnd, string *docID, string *url, string *base);

	static bool extractLinks(string document, vector< pair<string,string> > *results);
	
	static bool extractLinks(Index *index, offset documentStart,
			offset documentEnd, vector< pair<string,string> > *results);

	/** As above, but for Wikipedia pages instead of HTML documents. **/
	static bool analyzeWikipediaPage(string document,
			string *pageID, string *pageTitle,
			vector< pair<string,string> > *links);

	static bool analyzeWikipediaPage(Index *index, offset documentStart,
			offset documentEnd, string *pageID, string *pageTitle,
			vector < pair<string,string> > *links);

}; // end of class DocumentAnalyzer


#endif



