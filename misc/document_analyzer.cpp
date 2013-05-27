/**
 * author: Stefan Buettcher
 * created: 2006-07-16
 * changed: 2009-02-01
 **/


#include <string.h>
#include "document_analyzer.h"
#include "../query/getquery.h"


using namespace std;


static char *mystrcasestr(char *haystack, const char *needle) {
	int len = strlen(needle);
	for (int i = 0; haystack[i] != 0; i++)
		if ((haystack[i] | 32) == (*needle | 32))
			if (strncasecmp(&haystack[i], needle, len) == 0)
				return &haystack[i];
	return NULL;
} // end of mystrcasestr(char*, char*)


bool DocumentAnalyzer::analyzeTRECHeader(
		string document, string *docID, string *url, string *base) {
	char *doc = (char*)document.c_str();

	// check whether docid and url are available; if not, return false
	char *docno = mystrcasestr(doc, "<DOCNO>");
	if (docno == NULL)
		return false;
	char *dochdr = mystrcasestr(doc, "<DOCHDR>");
	if (dochdr == NULL)
		return false;

	// extract docid
	docno += 7;
	while ((*docno > 0) && (*docno <= 32))
		docno++;
	char *docnoEnd = docno;
	while ((*docnoEnd > ' ') && (*docnoEnd != '<'))
		docnoEnd++;
	*docnoEnd = 0;
	*docID = docno;

	// extract url
	char *urlStart = dochdr + 8;
	while ((*urlStart > 0) && (*urlStart <= 32))
		urlStart++;
	char *urlEnd = urlStart;
	while (*urlEnd > ' ')
		urlEnd++;
	*urlEnd = 0;
	*url = urlStart;

	*base = *url;
	char *base_href = mystrcasestr(&urlEnd[1], "<BASE HREF=\"");
	if (base_href != NULL) {
		base_href += 12;
		char *start = base_href;
		char *end = base_href;
		while ((*end != 0) && (*end != '"') && (*end != '<') && (*end != '>'))
			end++;
		if (*end == '"') {
			while ((*start > 0) && (*start <= 32))
				start++;
			while ((end[-1] > 0) && (end[-1] <= 32))
				end--;
			if (end > start) {
				*end = 0;
				*base = start;
			}
		}
	}
	
	return true;
} // end of analyzeTRECHeader(string, ...)


bool DocumentAnalyzer::analyzeTRECHeader(Index *index, offset documentStart,
		offset documentEnd, string *docID, string *url, string *base) {
	GetQuery *gq = new GetQuery(index, documentStart, documentEnd, false);
	if (!gq->parse()) {
		delete gq;
		return false;
	}
	char data[Query::MAX_RESPONSELINE_LENGTH + 1];
	if (!gq->getNextLine(data)) {
		delete gq;
		return false;
	}
	delete gq;
	return analyzeTRECHeader(data, docID, url, base);
} // end of analyzeTRECHeader(Index*, ...)


bool DocumentAnalyzer::analyzeWikipediaPage(string document,
		string *pageID, string *pageTitle, vector< pair<string,string> > *links) {
	*pageID = "n/a";
	int startOfID = document.find("<id>");
	if (startOfID != string::npos) {
		startOfID += 4;
		int endOfID = document.find("</id>", startOfID);
		if (endOfID != string::npos)
			*pageID = document.substr(startOfID, endOfID - startOfID);
	}

	*pageTitle = "n/a";
	int startOfTitle = document.find("<title>");
	if (startOfTitle != string::npos) {
		startOfTitle += 7;
		int endOfTitle = document.find("</title>", startOfTitle);
		if (endOfTitle != string::npos) {
			*pageTitle = document.substr(startOfTitle, endOfTitle - startOfTitle);
			normalizeString(pageTitle);
		}
	}

	links->clear();
	const char *ptr = document.c_str();
	while (strstr(ptr, "[[") != NULL) {
		ptr = strstr(ptr, "[[") + 2;
		const char *end = strstr(ptr, "]]");
		if (end == NULL)
			break;
		string link(ptr, end - ptr);
		string anchor, target;
		string::size_type vertical = link.find('|');
		if (vertical == string::npos) {
			anchor = link;
			target = link;
		}
		else {
			anchor = link.substr(vertical + 1);
			target = link.substr(0, vertical);
		}
		normalizeString(&target);
		normalizeString(&anchor);
		links->push_back(pair<string,string>(target, anchor));
	}
	return ((!pageID->empty()) && (!pageTitle->empty()));
} // end of analyzeWikipediaPage(string, ...)


bool DocumentAnalyzer::analyzeWikipediaPage(Index *index,
		offset documentStart, offset documentEnd, string *pageID,
		string *pageTitle, vector< pair<string,string> > *links) {
	GetQuery *gq = new GetQuery(index, documentStart, documentEnd, false);
	if (!gq->parse()) {
		delete gq;
		return false;
	}
	char data[Query::MAX_RESPONSELINE_LENGTH + 1];
	if (!gq->getNextLine(data)) {
		delete gq; 
		return false;
	} 
	delete gq;
	return analyzeWikipediaPage(data, pageID, pageTitle, links);
} // end of analyzeWikipediaPage(Index*, ...)


static string extractUntilQuot(char *data) {
	int found = -1;
	for (int i = 0; i < 32; i++) {
		if (data[i] == 0)	break;
		else if (data[i] == '"') { found = i; break; }
	}
	if (found > 0) {
		data[found] = 0;
		string result = data;
		data[found] = '"';
		return result;
	}
	return string();
}


static void extractAnchorText(char *data, string *result) {
	bool inTag = false, inImgTag = false;
	*result = "";
	char prev = 32;
	while (*data != 0) {
		if (*data == '<') {
			inTag = true;
			if (strncasecmp(data, "<img", 4) == 0)
				inImgTag = true;
		}
		else if (*data == '>') {
			inTag = false;
		}
		else if (inTag) {
			if (!inImgTag) {
				data++;
				continue;
			}
			inImgTag = false;
			char *endOfTag = strchr(data, '>');
			if (endOfTag == NULL)
				return;
			*endOfTag = 0;
			char *alt = mystrcasestr(data, "alt=\"");
			char *title = mystrcasestr(data, "title=\"");
			*endOfTag = '>';
			if (alt != NULL)
				*result += " " + extractUntilQuot(&alt[5]);
			if (title != NULL)
				*result += " " + extractUntilQuot(&title[7]);
		}
		else {
			if ((*data > 0) && (*data <= 32)) {
				*data = 32;
				if (*data != prev) {
					prev = *data;
					*result += prev;
				}
			}
			else {
				prev = *data;
				*result += prev;
			}
		}
		data++;
	}
} // end of extractAnchorText(char*, string*)


bool DocumentAnalyzer::extractLinks(
		string document, vector< pair<string,string> > *results) {
	char *doc = (char*)document.c_str();
	results->clear();

	string anchorText, url;

	char *anchor = NULL;
	while ((anchor = mystrcasestr(doc, "<a")) != NULL) {
		url = anchorText = "";
		anchor += 2;
		doc = anchor;
		if ((*anchor <= 0) || (*anchor > 32))
			continue;
		for (int i = 0; anchor[i] != 0; i++) {
			if ((anchor[i] == '<') || (anchor[i] == '>')) {
				anchor = &anchor[i];
				break;
			}
			if (strncasecmp(&anchor[i], "href", 4) == 0) {
				anchor = &anchor[i];
				break;
			}
		}
		if (strncasecmp(anchor, "href", 4) != 0) {
			doc = anchor;
			continue;
		}

		anchor = &anchor[4];
		while ((*anchor > 0) && (*anchor <= 32))
			anchor++;
		if (*anchor != '=')
			continue;
		anchor++;
		while ((*anchor > 0) && (*anchor <= 32))
			anchor++;
		if ((*anchor == '"') || (*anchor == '\'')) {
			char delimiter = *anchor;
			anchor++;
			while ((*anchor > 0) && (*anchor <= 32))
				anchor++;
			char *end = anchor;
			while ((*end != delimiter) && (*end != '<') && (*end != '>') && (*end != 0))
				end++;
			if (*end != delimiter) {
				doc = end;
				continue;
			}
			char c = *end;
			*end = 0;
			url = anchor;
			*end = c;
			doc = end;
		}
		else if (*anchor > 32) {
			char *end = anchor;
			while ((*end > 32) && (*end != '<') && (*end != '>'))
				end++;
			if ((*end == '<') || (*end < 0)) {
				doc = end;
				continue;
			}
			char c = *end;
			*end = 0;
			url = anchor;
			*end = c;
			doc = end;
		}

		if (!url.empty()) {
			char *endOfTag = strchr(doc, '>');
			if (endOfTag == NULL)
				continue;
			char *closingTag = mystrcasestr(doc, "</a>");
			if ((closingTag == NULL) || (closingTag < endOfTag))
				continue;

			*closingTag = 0;
			extractAnchorText(&endOfTag[1], &anchorText);
			doc = &closingTag[1];
		}

		if ((!url.empty()) && (!anchorText.empty())) {
			if (strncasecmp(url.c_str(), "mailto:", 7) == 0)
				continue;
			if (strchr(url.c_str(), '\n') != NULL)
				continue;

			char tmp[256];
			if (url.length() > 200)
				continue;
			strcpy(tmp, url.c_str());
			char *u = tmp, *u2;
			while ((u2 = mystrcasestr(&u[1], "http://")) != NULL)
				u = u2;
			char *hash = strchr(u, '#');
			if (hash != NULL)
				*hash = 0;
			url = u;
			string a;
			char prev = 0;
			for (int i = 0; i < anchorText.length(); i++) {
				if ((anchorText[i] > 0) && (anchorText[i] <= 32)) {
					if (prev != ' ')
						a += ' ';
					prev = ' ';
				}
				else {
					prev = anchorText[i];
					a += prev;
				}
			}
			anchorText = a;
			results->push_back(pair<string,string>(url, anchorText));
		}
	}

	return (results->size() > 0);
} // end of extractLinks(string, vector*)


bool DocumentAnalyzer::extractLinks(Index *index, offset documentStart,
		offset documentEnd, vector< pair<string,string> > *results) {
	GetQuery *gq = new GetQuery(index, documentStart, documentEnd, false);
	if (!gq->parse()) {
		delete gq;
		return false;
	}
	char data[Query::MAX_RESPONSELINE_LENGTH + 1];
	if (!gq->getNextLine(data)) {
		delete gq;
		return false;
	}
	delete gq;
	return extractLinks(data, results);
} // end of extractLinks(Index*, ...)


