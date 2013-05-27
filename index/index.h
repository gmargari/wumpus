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
 * Definition of the Index class. Index is (as you might have suspected)
 * the central class of the indexer.
 *
 * author: Stefan Buettcher
 * created: 2004-09-14
 * changed: 2007-12-17
 **/


#ifndef __INDEX__INDEX_H
#define __INDEX__INDEX_H


#include "annotator.h"
#include "index_types.h"
#include "lexicon.h"
#include "indextotext.h"
#include "../filemanager/filemanager.h"
#include "../filemanager/securitymanager.h"
#include "../misc/all.h"
#include "../filesystem/filesystem.h"


class AuthConnDaemon;
class ConnDaemon;
class DocIdCache;
class FileManager;
class FileSysDaemon;
class IndexCache;
class LanguageModel;
class OnDiskIndexManager;


class Index : public Lockable {

	friend class ClientConnection;
	friend class GCLQuery;
	friend class MasterDocIdCache;
	friend class MasterIndex;
	friend class OnDiskIndexManager;
	friend class Query;
	friend class TerabyteLexicon;

public:

	/**
	 * We use the following constants to distinguish between Index and MasterIndex.
	 * "indexType" has the appropriate value (set in the constructor).
	 **/
	static const int TYPE_INDEX = 1;
	static const int TYPE_MASTERINDEX = 2;
	static const int TYPE_FAKEINDEX = 3;

	/** We will not index files that are bigger than this. **/
	static const int64_t DEFAULT_MAX_FILE_SIZE = 20000000000LL;
	configurable int64_t MAX_FILE_SIZE;

	/** We will not index files that are smaller than this. **/
	static const int64_t DEFAULT_MIN_FILE_SIZE = 8;
	configurable int64_t MIN_FILE_SIZE;

	/** How much memory do we want to allocate for in-memory UpdateLists? **/
	static const int DEFAULT_MAX_UPDATE_SPACE = 40 * 1024 * 1024;
	configurable int MAX_UPDATE_SPACE;

	/** Maximum number of processes holding a read lock at the same time. **/
	static const int DEFAULT_MAX_SIMULTANEOUS_READERS = 4;
	configurable int MAX_SIMULTANEOUS_READERS;

	/** Stemming level can be between 0 and 2. See documentation for details. **/
	static const int DEFAULT_STEMMING_LEVEL = 0;
	configurable int STEMMING_LEVEL;

	/** Tells us whether file permissions have to be respected in query processing. **/
	static const bool DEFAULT_APPLY_SECURITY_RESTRICTIONS = true;
	configurable bool APPLY_SECURITY_RESTRICTIONS;

	/** Listen port in case we have a TCP server running. -1 means: no server. **/
	static const int DEFAULT_TCP_PORT = -1;
	configurable int TCP_PORT;

	/** Do we have the FileSysDaemon running and reading from /proc/fschange? **/
	static const bool DEFAULT_MONITOR_FILESYSTEM = false;
	configurable bool MONITOR_FILESYSTEM;

	/**
	 * Tells us whether we have to keep track of term occurrences per document.
	 * 0 means: no document-level information; 1 means: both positional and document-
	 * level; 2 means: only document-level information.
	 **/
	static const int DEFAULT_DOCUMENT_LEVEL_INDEXING = 0;
	configurable int DOCUMENT_LEVEL_INDEXING;

	/**
	 * If we enable XPath support, we have to add special tags into the
	 * index at indexing time: for every opening tag at nesting level (depth)
	 * N, we have a "<level!N>" in the index. For every corresponding closing
	 * tag, we have "</level!N>". This is necessary because we want to quickly
	 * find parents and children, so we have to know nesting levels.
	 **/
	static const bool DEFAULT_ENABLE_XPATH = false;
	configurable bool ENABLE_XPATH;

	/**
	 * If this is set to true, we add token bigrams into the index in addition
	 * to individual tokens.
	 **/
	static const bool DEFAULT_BIGRAM_INDEXING = false;
	configurable bool BIGRAM_INDEXING;

	/** For managing file permissions etc. The Superuser can read everything. **/
	static const uid_t SUPERUSER = (uid_t)0;

	/** GOD is even mightier than the super-user. He can see deleted files as well. **/
	static const uid_t GOD = (uid_t)-1;

	/** User nobody. Can only access files with world-wide read permission. **/
	static const uid_t NOBODY = (uid_t)-2;

	/** Where do we store our temporary data? **/
	static const char *TEMP_DIRECTORY;

	/** "Index". **/
	static const char *LOG_ID;

	/**
	 * Whenever we have more than 1 process trying to update the index, one process
	 * has to wait for the other one to finish. UPDATE_WAIT_INTERVAL is the interval
	 * between two subsequent poll operations by the waiting process.
	 **/
	static const int INDEX_WAIT_INTERVAL = 20;

	/** Maximum number of queries processed in parallel. **/
	static const int MAX_REGISTERED_USERS = 4;

	/**
	 * We refuse to run the garbage collection if the number of garbage postings
	 * in the index is smaller than this value.
	 **/
	static const int MIN_GARBAGE_COLLECTION_THRESHOLD = 64 * 1024;

	/** This is our working directory. **/
	char *directory;

	/**
	 * This is the directory below which we can index files. An empty string means:
	 * everything may be indexed.
	 **/
	char baseDirectory[MAX_CONFIG_VALUE_LENGTH];

public:

	/** TRUE iff this index is a child of a MasterIndex instance. **/
	bool isSubIndex;

	/** TYPE_INDEX or TYPE_MASTERINDEX. **/
	int indexType;

	/** Indicates whether this Index is read-only. **/
	bool readOnly;

	bool shutdownInitiated;

	/**
	 * Tells us whether the on-disk image of this index is in a consistent state.
	 * We use this in order to decide whether we load an on-disk index or refuse
	 * to do so and create a new one inside the constructor.
	 **/
	bool isConsistent;

	/**
	 * UID of the user that has started the process. We assume that this user (and
	 * the superuser) have full access to the index. For every other user, the
	 * result will be filtered by the SecurityManager.
	 **/
	uid_t indexOwner;

	/** List of users registered with the Index. **/
	int64_t registeredUsers[MAX_REGISTERED_USERS];

	/** Number of registered users. **/
	int registeredUserCount;

	/** Counter used to give unique user IDs to Query instances using us. **/
	int64_t registrationID;

	/**
	 * Here, we count the number of content update operations (WRITE, UNLINK)
	 * performed. This is just for the curious. No special functionality.
	 **/
	unsigned int updateOperationsPerformed;

	/** We set this to MAX_REGISTERED_USERS initially and simply do P and V then. **/
	sem_t registeredUserSemaphore;

	/** Used to restrict the number of concurrent update operations to 1. **/
	sem_t updateSemaphore;

	/** We allow annotations inside the index for faster DOCID retrieval. **/
	Annotator *annotator;

	/** This is where all the file information is stored. **/
	FileManager *fileManager;

	/** 
	 * Internal index cache, used to speed up query processing by caching frequently
	 * used lists, such as "<doc>".."</doc>".
	 **/
	IndexCache *cache;

	/** In-memory compressed list of all document IDs. **/
	DocIdCache *documentIDs;

	/** On-disk compressed versions of recently accessed PDF files etc. **/
	DocumentCache *documentCache;

	/** Mapping from index positions to file positions, for faster "@get" queries. **/
	IndexToText *indexToTextMap;

	/** The guy who defined merge strategies etc. **/
	OnDiskIndexManager *indexManager;

	/** This is the guy that tells us which extents can be seen by which user. **/
	SecurityManager *securityManager;

	/** Daemon waiting for unauthenticated TCP connections. **/
	ConnDaemon *connDaemon;

	/** Daemon watching the filesystem. **/
	FileSysDaemon *fileSysDaemon;

	/**
	 * This flag is used to determine if an update query is currently processed.
	 * If so, the execution of other update queries is delayed until the first
	 * query has been finished.
	 **/
	bool indexIsBeingUpdated;

	/** Used to trigger the garbage collection. **/
	offset usedAddressSpace, deletedAddressSpace;

	/** Garbage collection threshold values. **/
	double garbageThreshold, onTheFlyGarbageThreshold;

	/**
	 * The biggest offset value we have encountered so far. This helps us determine
	 * whether a notifyOfAddressSpaceChange operation actually deletes postings or
	 * merely readjusts overallocation of address space during the indexing process.
	 **/
	offset biggestOffsetSeenSoFar;

	/** Used for bigram indexing. **/
	InputToken previousToken;

public:

	/** Default constructor. **/
	Index();

	/**
	 * Creates a new Index instance from the data found in the directory
	 * specified by parameter "directory". If there is no data, a new, empty
	 * index is created. If the directory does not exist, it will be created.
	 * If "isSubIndex" is true, that means we are a child of a bigger index,
	 * probably the system-wide MasterIndex. If we are a sub-index, we are not
	 * allowed to have our own connection daemons.
	 **/
	Index(const char *directory, bool isSubIndex);

	/** Deletes the object. **/
	virtual ~Index();

	/**
	 * Notifies the Index that a particular event has taken place. The event
	 * description ("event") follows the fschange event syntax.
	 **/
	virtual int notify(const char *event);

	/**
	 * Informs the Index that a file starting at "start" and ending at "end" has
	 * either be added to or removed from the index. This is necessary in order to
	 * keep the "used address space" stuff up-to-date.
	 **/
	virtual void notifyOfAddressSpaceChange(int signum, offset start, offset end);

	/**
	 * Returns the posting list for the given term. If the term does not have any
	 * postings, an empty PostingList instance is returned. Caller has to free
	 * memory.
	 * Please note that this list is already filtered in order to reflect the
	 * user's file permissions.
	 **/
	virtual ExtentList *getPostings(const char *term, uid_t userID);

	/**
	 * As above, but lets the caller decide whether he wants to get all postings
	 * from the on-disk indices, all postings from the in-memory update index,
	 * or both.
	 **/
	virtual ExtentList *getPostings(const char *term, uid_t userID, bool fromDisk, bool fromMemory);

	/**
	 * Same as before, but allows to fetch the posting lists for multiple terms
	 * in a single call. This allows the index manager to schedule index accesses
	 * in a more sensible way. The resulting ExtentList instances are put into the
	 * array referenced by "results". Memory for all "termCount" instances created
	 * needs to be freed by the caller later on.
	 * It is legal to pass NULL pointers for some terms, in which case the
	 * corresponding entry in the "results" table will be set to NULL in response.
	 **/
	virtual void getPostings(char **terms, int termCount, uid_t userID, ExtentList **results);

	/**
	 * Adds the annotation given by "annotation" to the annotation database for
	 * index position "position". If there is already an annotation for that index
	 * position, the old annotation will be erased.
	 **/
	virtual void addAnnotation(offset position, const char *annotation);

	/**
	 * Prints the annotation found at position "position" into the buffer given
	 * by "buffer". If no annotation can be found, buffer will equal "\0".
	 **/
	virtual void getAnnotation(offset position, char *buffer);

	/** Removes the annotation stored at index position "position", if existent. **/
	virtual void removeAnnotation(offset position);

	/** Returns the biggest offset value in any of the files in the index. **/
	virtual offset getBiggestOffset();

	/**
	 * Returns the document type ID of the file given by "fullPath". -1 if the
	 * file does not exist in the index.
	 **/
	virtual int getDocumentType(const char *fullPath);

	/**
	 * Gives us the highest index offset "o" such that "o <= where" and the
	 * corresponding file offset inside the file that the index offset belongs
	 * to. Returns true if such an offset exists (result stored in "indexPosition"
	 * and "filePosition"), false otherwise.
	 **/
	virtual bool getLastIndexToTextSmallerEq(offset where, offset *indexPosition, off_t *filePosition);

	/** Returns the UID of the index owner. **/
	virtual uid_t getOwner();

	/**
	 * Returns a list of all extents visible by the user given ("userID"). These
	 * extents correspond to files. If "merge == true", the extents may be merged
	 * in order to decrease memory consumption.
	 **/
	virtual VisibleExtents *getVisibleExtents(uid_t userID, bool merge);

	/** Returns lower and upper bound for the size of the dictionary. **/
	virtual void getDictionarySize(offset *lowerBound, offset *upperBound);

	/**
	 * Whenever a query wants to use the data stored in the index, it has to
	 * register first. When it has finished execution, it has to deregister. This
	 * way, we know who is still using us and can apply internal changes, such
	 * as an increase in the number of CompactIndex instances accordingly. If the
	 * user reads a return value < 0, that means the registration failed. Index
	 * data may not be used.
	 **/
	virtual int64_t registerForUse();

	virtual int64_t registerForUse(int64_t suggestedID);

	/**
	 * Deregisters a user. "id" is the user ID obtained by calling registerForUse().
	 * This function *has* to be called when a Query instance is done.
	 **/
	virtual void deregister(int64_t id);

	/**
	 * Waits for all registered queries to finish execution. After
	 * waitForUsersToFinish() has been called, registerForUse() will always return
	 * -1.
	 **/
	virtual void waitForUsersToFinish();

	/** Puts a textual description of the index status/content into "buffer". **/
	virtual void getIndexSummary(char *buffer);

	/** Returns the current query timestamp. **/
	virtual int64_t getTimeStamp(bool withLocking);

	/**
	 * Tries to fetch the ExtentList produced by the given query from the index cache.
	 * If successful, an ExtentList instance is returned. If not, NULL.
	 **/
	virtual ExtentList *getCachedList(const char *queryString);

	/** Returns a reference to this Index instance's IndexCache. **/
	virtual IndexCache *getCache();

	/** Returns the DocumentCache instance associated with this index. **/
	virtual DocumentCache *getDocumentCache(const char *fileName);

	/** Returns true if we are allowed to index "directoryName". **/
	static bool directoryAllowed(const char *directoryName);

	/** Returns true iff the given user may access (read) the given file. **/
	virtual bool mayAccessFile(uid_t userID, const char *path);

	/**
	 * Inserts the file specified by "fileName" into the index. Returns the status
	 * code of the operation (RESULT_SUCCESS or an error code). The file type can
	 * either be detected automatically ("fileType == NULL") or specified explicitly
	 * (using the appropriate value for "fileType" -- as defined in FilteredInputStream).
	 **/
	int addFile(const char *fileName, const char *fileType);

	/**
	 * Returns the language model defined in the file given by the configuration
	 * variable STATIC_LANGUAGE_MODEL. Tries to keep the language model in the
	 * cache in order to avoid costly reload operations.
	 **/
	virtual LanguageModel *getStaticLanguageModel();

	/**
	 * Writes all pending updates to disk. Depending on the index update strategy,
	 * this may or may not involve index merge operations. Depending on whether
	 * we are in synchronous or asynchronous update mode, those updates may or
	 * may not be carried out in the background.
	 **/
	virtual void sync();

protected:

	/** Obtains configuration information from the config manager. **/
	virtual void getConfiguration();

	/** Sets the mount point of this Index's FileManager component. **/
	virtual void setMountPoint(const char *mountPoint);

	/** Prints the class name of this object into the given buffer. **/
	virtual void getClassName(char *target);

	/** Loads index information from the main index file. **/
	void loadDataFromDisk();

	/** Writes index information to data file. **/
	void saveDataToDisk();

	/**
	 * Sets the Index's consistency flag to "false". If it was "true" before
	 * before the method was called, then data is written to disk in order to
	 * ensure we see the inconsistency the next time the Index is loaded.
	 **/
	void markAsInconsistent();

	/**
	 * Invalidates the current content of the cache and load new data, as specified
	 * by the CACHED_EXPRESSIONS configuration value.
	 **/
	void invalidateCacheContent();

	/**
	 * Adds the given list of postings, defined by their InputToken elements,
	 * to the index. Automatically adds bigrams to the index in case bigram
	 * indexing is enabled.
	 **/
	void addPostingsToIndex(InputToken* tokens, int tokenCount);

	/**
	 * Term string preprocessor called from getPostings(...). Takes care of stemming
	 * and handling illegal input strings.
	 **/
	void preprocessTerm(char *term);

}; // end of class Index


#endif


