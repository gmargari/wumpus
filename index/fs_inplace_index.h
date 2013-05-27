/**
 * This class defines the FS_InplaceIndex class. FS_InplaceIndex uses the file
 * system as the index storage layer, storing each posting list in a separate
 * file on disk.
 *
 * author: Stefan Buettcher
 * created: 2007-02-10
 * changed: 2007-02-11
 **/


#ifndef __INDEX__FS_INPLACE_INDEX_H
#define __INDEX__FS_INPLACE_INDEX_H


#include "inplace_index.h"


class ExtentList;
class Index;
class PostingListInFile;


class FS_InPlaceIndex : public InPlaceIndex {

private:

	/** Maximum length of the base directory. **/
	static const int MAX_BASEDIR_LENGTH = 255;

	/** Path to the base directory of this in-place index. **/
	char baseDirectory[MAX_BASEDIR_LENGTH + 1];

	/** Maximum length of the path to a file in the in-place index. **/
	static const int MAX_FILEPATH_LENGTH = MAX_BASEDIR_LENGTH + MAX_TOKEN_LENGTH + 32;

	/** Number of distinct terms in the index. **/
	int termCount;

	/** Total size of this in-place (combined size of all files), in bytes. **/
	int64_t byteSize;

	/** Total number of postings in the in-place index. **/
	int64_t postingCount;

	/** Posting list that is currently being updated (if any). **/
	PostingListInFile *listBeingUpdated;
	int64_t currentListsOriginalSize;

	/** Number of file updates performed in total. **/
	long long fileUpdateCnt;

public:

	/**
	 * Creates a new InPlaceIndex that stores its data in the given directory.
	 * If the directory already exists, index data in the directory will be
	 * re-used.
	 **/
	FS_InPlaceIndex(Index *owner, const char *directory);

	/** Boring destructor. **/
	~FS_InPlaceIndex();

	/**
	 * Adds the given list of uncompressed postings to the on-disk posting list
	 * for the given term.
	 **/
	virtual void addPostings(const char *term, offset *postings, int count);

	/**
	 * Adds the given list of compressed postings to the on-disk posting list
	 * for the given term. "byteLength" is the total size of the compressed
	 * postings. "count" is the number of postings. "first" and "last" are the
	 * values of the first and the last posting in the list.
	 **/
	virtual void addPostings(const char *term, byte *compressedPostings, int byteLength,
	                         int count, offset first, offset last);

	/**
	 * Returns the posting list for the given term. If the term's on-disk posting
	 * list is non-empty, then this will be an instance of SegmentedPostingList.
	 * Otherwise, an ExtentList_Empty instance is returned.
	 **/
	virtual ExtentList *getPostings(const char *term);
	
	/** Returns the number of terms in the in-place index. **/
	virtual int64_t getTermCount();

	/** Returns the total size of all files in the in-place index. **/
	virtual int64_t getByteSize();

	/** Returns the total number of postings stored in the in-place index. **/
	virtual int64_t getPostingCount();

	/** Returns the name of the index directory that contains the posting files. **/
	virtual char *getFileName();

	virtual void finishUpdate();

private:

	/**
	 * Writes the full file path of the file containing the postings for the given
	 * term into the given output buffer. The buffer referenced by "filePath" needs
	 * to have enough space for at least strlen(baseDirectory) + 2 * MAX_TOKEN_LENGTH
	 * bytes.
	 **/
	void getFilePathForTerm(const char *term, char *filePath);

	/** Returns a PostingListInFile object describing the posting list of the given term. **/
	PostingListInFile *getPostingListInFile(const char *term);

	void addNewTerm(const char *term);
	
}; // end of class FS_InPlaceIndex


#endif


