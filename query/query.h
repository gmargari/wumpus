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
 * The Query class is the superclass for all types of queries. We support a large
 * number of different query types. Look at the "isValidCommand" methods of the
 * various Query classes to see what's possible.
 *
 * I have implemented several abbreviations:
 *
 *   @okapi {GCL_EXPRESSION}+  =>  @rank[bm25] "<doc>".."</doc>" by {GCL_EXPRESSION}+
 *   @qap   {GCL_EXPRESSION}+  =>  @rank[qap] "<doc>".."</doc>" by {GCL_EXPRESSION}+
 *
 * author: Stefan Buettcher
 * created: 2004-09-26
 * changed: 2009-02-01
 **/


#ifndef __QUERY__QUERY_H
#define __QUERY__QUERY_H


#include <string.h>
#include <sys/times.h>
#include "../index/index.h"
#include "../filters/inputstream.h"
#include "../misc/stringtokenizer.h"


extern const char * DOC_QUERY;
extern const char * DOCNO_QUERY;
extern const char * FILE_QUERY;
extern const char * EMPTY_MODIFIERS[1];

class GCLQuery;


class Query : public Lockable {

public:

	/** By default, return at most 20 result lines for every query type. **/
	static const int DEFAULT_COUNT = 20;

	/** Maximum limit is 50 million result lines for now. **/
	static const int MAX_COUNT = 50000000;

	static const int MAX_RESPONSELINE_LENGTH = FilteredInputStream::MAX_FILTERED_RANGE_SIZE + 4;

	/** Queries that are longer than this will not be processed. **/
	static const int MAX_QUERY_LENGTH = 8192;

	/**
	 * If no other value is specified in the configuration file, we limit the
	 * of memory consumed by the query to this.
	 **/
	static const int DEFAULT_MEMORY_LIMIT = 32 * 1024 * 1024;

	static const int STATUS_OK = 0;
	static const int STATUS_ERROR = 1;

	/** Definition of several query type constants, used by getType(). **/
	static const int QUERY_TYPE_UNKNOWN = -1;
	static const int QUERY_TYPE_MISC = 1;
	static const int QUERY_TYPE_UPDATE = 2;	
	static const int QUERY_TYPE_RANKED = 3;
	static const int QUERY_TYPE_GET = 4;
	static const int QUERY_TYPE_HELP = 5;

	/** Maximum number of modifiers allowed in a query. **/
	static const int MAX_MODIFIER_COUNT = 32;

protected:

	/** The Index instance we are working with. **/
	Index *index;

	/** Which user issues the query? We need to know this for the SecurityManager. **/
	uid_t userID;

	/** GCLQuery, RankedQuery, ... whatever. **/
	Query *actualQuery;

	/** Does what it says. **/
	bool syntaxErrorDetected, ok;

	/** Indicates if we have more extents to read or if we are done. **/
	bool finished;

	/**
	 * This object contains the list of visible extents (i.e. searchable files) for
	 * the user that this query is associated with.
	 ***/
	VisibleExtents *visibleExtents;

	/**
	 * Tells us whether the "visibleExtents" object has been created by the constructor
	 * (in which case it has to be deleted in the destructor) or if it has been created
	 * by somebody else.
	 **/
	bool mustFreeVisibleExtentsInDestructor;

	/** How much memory are we allowed to consume when processing this query? **/
	int memoryLimit;

	/** When did we create the query instance? **/
	int startTime;

	/** Same as above, but used to compute CPU utilization. **/
	struct tms cpuStartTime;

	/** Used to send messages to the logging service. **/
	char errorMessage[256];

	/** For various purposes. **/
	char scrap[256];

	/** String used to create this query instance. **/
	char *queryString;

	/** These are standard modifiers, parsed in the Query::processModifiers() method. **/
	bool verbose, printFileName, printPageNumber, printDocumentID;

	/** Some text to print if user asks for verbosity. NULL if no text. **/
	char *verboseText;

	/**
	 * Another standard modifier: The tokenizer selected to tokenizer the query
	 * elements.
	 **/
	char *queryTokenizer;

	/** Another standard modifier: How many result lines have to be returned? **/
	int count;

	/** Another standard modifier: Are we going to use the cache for this query? **/
	bool useCache;

	/**
	 * Flags that tell us whether posting lists have to be fetched exclusively from
	 * the on-disk indices or the in-memory index, respectively.
	 **/
	bool onlyFromDisk, onlyFromMemory;

	/** The additional guy, specified by "[add=...]" or "[addget=...]". **/
	GCLQuery *additionalQuery;

	/**
	 * Tells us whether we have to perform a GET operation on the additional
	 * guy in case he is present.
	**/
	bool addGet;

	/** Tells us whether the "[getannotation]" modifier is used. **/
	bool getAnnotation;

	/**
	 * We have to register with the Index as a user. This is the unique ID the
	 * Index gave us.
	 **/
	int64_t indexUserID;

private:

	/** Tells us whether we are in fact of type Query and not of one of the subtypes. **/
	bool I_AM_THE_REAL_QUERY;

	/** Common method called by all constructors. **/
	void initialize();

public:

	/** Default constructor. **/
	Query();

	/**
	 * Creates a new Query instance for the Index given by "index". "queryString"
	 * contains the complete query, including command and modifiers, e.g.
	 * @gcl[count=30] ("<html>".."</html>")/>"<title>".
	 **/
	Query(Index *index, const char *queryString, int userID);

	virtual ~Query();

	virtual bool parse();

	/**
	 * Retrieves the next response line and writes it into the buffer given by
	 * "line". If there is no more data to return, it returns false.
	 **/
	virtual bool getNextLine(char *line);

	/**
	 * If the execution has been finished, it returns true (false otherwise).
	 * Writes the status code into "code" and a description into "description".
	 **/
	virtual bool getStatus(int *code, char *description);

	/** Returns the type of the Query object. **/
	virtual int getType();

	/** Returns the number of results to this query. **/
	virtual int getCount();

	/**
	 * Returns a copy of "query", where all macros have been replaced by their
	 * defined values.
	 **/
	static char *replaceMacros(const char *query);

	/** Returns a copy of this query's query string. **/
	virtual char *getQueryString();

	static bool getStatusSyntaxError(int *code, char *description) {
		*code = STATUS_ERROR;
		strcpy(description, "Syntax error.");
		return true;
	}

	static bool getStatusOk(int *code, char *description) {
		*code = STATUS_OK;
		strcpy(description, "Ok.");
		return true;
	}

protected:

	/**
	 * Returns an ExtentList instance corresponding to the posting list of the
	 * given term, as seen by the given user.
	 **/
	virtual ExtentList * getPostings(const char *term, uid_t userID);

	/**
	 * Processes the given list of modifiers, filling in values for standard
	 * query modifiers.
	 **/
	virtual void processModifiers(const char **modifiers);

	/**
	 * Scans the NULL-terminated list of modifiers given, searching for the modifier
	 * "name". If it can be found in the list, its value is returned. Otherwise,
	 * "defaultValue" is returned.
	 **/
	bool getModifierBool(const char **modifiers, const char *name, bool defaultValue);

	/** Analogous to getModifierBool(...). **/
	int getModifierInt(const char **modifiers, const char *name, int defaultValue);

	/** Analogous to getModifierBool(...). **/
	double getModifierDouble(const char **modifiers, const char *name, double defaultValue);

	/**
	 * Analogous to getModifierBool(...). Memory has to be freed by the caller.
	 * "defaultValue" may have NULL value, in which case NULL is returned by the
	 * method as well (in case no matching modifier can be found).
	 **/
	char *getModifierString(const char **modifiers, const char *name, const char *defaultValue);

	/**
	 * Returns true iff the user associated with this query is allows to access
	 * the index extend beginning at "start" and ending at "end".
	 **/
	bool mayAccessIndexExtent(offset start, offset end);

	/**
	 * Adds the filename of the file containing the index position "posInFile" to
	 * the string given by "line".
	 **/
	void addFileNameToResultLine(char *line, offset posInFile);

	/**
	 * Adds the page number of the page that contains the extent from "startPos" to
	 * "endPos" to the result line.
	 **/
	void addPageNumberToResultLine(char *line, offset startPos, offset endPos);

	/** Adds the annotation found at offset "startPos" to the line. **/
	void addAnnotationToResultLine(char *line, offset startPos);

	/**
	 * Writes the document ID of the document containing the given index extent
	 * (startPos, endPos) into the result string. If there is no such document (or
	 * no ID), "n/a" is returned. "isDocStart" indicates whether the given address
	 * "startPos" is the actual start position of a document or whether it only
	 * lies within some document.
	 **/
	void getDocIdForOffset(char *result, offset startPos, offset endPos, bool isDocStart);

	/**
	 * Adds the information got from the "[add=...]" or "[addget=...]" part to
	 * the given line.
	 **/
	void addAdditionalStuffToResultLine(char *line, offset startPos, offset endPos);

	/**
	 * Adds a line of the form "# key: value" to the verboseText of the query.
	 * If key == NULL, then it adds a line of the form "# value".
	 **/
	void addVerboseString(const char *key, const char *value);

	/**
	 * Adds a line of the form "# key: %.4lf(value)" to the verboseText of the
	 * query. If key == NULL, then it adds a line of the form "# %.4lf(value)".
	 **/
	void addVerboseDouble(const char *key, double value);

public:

	/** Returns true iff "c" is a whitespace character. **/
	static inline bool isWhiteSpace(char c) {
		return ((c > 0) && (c <= ' '));
	}

	/** Prints the offset value given into the string buffer given. **/
	static inline void printOffset(offset o, char *s) {
		sprintf(s, "%lld", static_cast<long long>(o));
	}

}; // end of class Query




/**
 * The following functions are used to register a new query class with the
 * query dispatcher (part of the Query class). They are also used to pass
 * some information that can be used by the HelpQuery class to print helpful
 * information about the usage of different query commands.
 **/

#ifdef __IN_QUERY_CPP__
	#define REGISTER_QUERY_CLASS(CLASS_NAME, CMD_STRING, SUMMARY, DESCRIPTION) \
		static Query *create##CLASS_NAME##CMD_STRING(Index *index, const char *command, const char **modifiers, const char *body, uid_t userID, int memoryLimit) { \
			return new CLASS_NAME(index, command, modifiers, body, userID, memoryLimit); \
		} \
		static bool rdummy##CLASS_NAME##CMD_STRING = registerQueryClass(#CMD_STRING, &create##CLASS_NAME##CMD_STRING); \
		static bool hdummy##CLASS_NAME##CMD_STRING = registerQueryHelpText(#CMD_STRING, SUMMARY, DESCRIPTION);
#else
	#define REGISTER_QUERY_CLASS(CLASS_NAME, CMD_STRING, SUMMARY, DESCRIPTION)
#endif

#define REGISTER_QUERY_CLASS_2(CLASS_NAME, CMD_STRING, SUMMARY, DESCRIPTION) \
	static Query *create##CLASS_NAME##CMD_STRING(Index *index, const char *command, const char **modifiers, const char *body, uid_t userID, int memoryLimit) { \
		return NULL; \
	} \
	static bool rdummy##CLASS_NAME##CMD_STRING = registerQueryClass(#CMD_STRING, &create##CLASS_NAME##CMD_STRING); \
	static bool hdummy##CLASS_NAME##CMD_STRING = registerQueryHelpText(#CMD_STRING, SUMMARY, DESCRIPTION);

#define REGISTER_QUERY_ALIAS(CMD_STRING, ALIAS) \
	static bool rdummy##CMD_STRING##ALIAS = registerQueryAlias(#CMD_STRING, #ALIAS);


typedef Query* (*QueryFactoryMethod)(Index*, const char*, const char**, const char*, uid_t, int);

/**
 * Registers the given command with the query dispatcher. Returns false iff
 * there already is an entry for the given command.
 **/
bool registerQueryClass(const char *cmdString, QueryFactoryMethod factoryMethod);

/**
 * Adds an alias command for the given query command. Returns false iff
 * "cmdString" does not exist or if "aliasCmdString" is already used by
 * somebody else.
 **/
bool registerQueryAlias(const char *cmdString, const char *aliasCmdString);

/**
 * Registers the given help text with the given query command. This information
 * is used at run-time by the HelpQuery class to print meaningful information
 * about each query type.
 **/
bool registerQueryHelpText(const char *cmdString, const char *summary, const char *helpText);

/**
 * Returns a copy of the help text for the given query type (indicated per
 * "commandString"); NULL if there is no such query type. Memory has to be
 * freed by the caller.
 **/
char *getQueryHelpText(const char *cmdString);

/** Returns a summary list of all available query commands. **/
char *getQueryCommandSummary();

/**
 * Returns a pointer to the factory method for the given query type of NULL if
 * no such query type exists.
 * Result needs to be cast to Query*(*)(Index*, char*, char**, char*, uid_t)
 **/
QueryFactoryMethod getQueryFactoryMethod(const char *cmdString);


REGISTER_QUERY_CLASS_2(GCLQuery, query,
	"Has no functionality, but provides modifiers to other query commands.",
	"Query modifiers supported:\n\n" \
	"  boolean verbose (default: false)\n" \
	"    prints additional information about the internal query structure\n" \
	"  boolean filename (default: false)\n" \
	"    for each search result, prints the name of the file containing it\n" \
	"  boolean docid (default: false)\n" \
	"    for each search result, prints the TREC docid (if available)\n\n" \
	"The modifiers supported by @query are available to all other commands, too."
)


#endif


