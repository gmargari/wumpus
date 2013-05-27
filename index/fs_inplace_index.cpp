/**
 * Implementation of the FS_InPlaceIndex class. See header file for documentation.
 *
 * author: Stefan Buettcher
 * created: 2007-02-10
 * changed: 2007-02-10
 **/


#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include "fs_inplace_index.h"
#include "postinglist_in_file.h"
#include "../misc/all.h"


static const char *LOG_ID = "FS_InPlaceIndex";


FS_InPlaceIndex::FS_InPlaceIndex(Index *owner, const char *directory) {
	// initialize variables and make sure index directory exists
	this->owner = owner;
	this->directory = duplicateString(directory);
	assert(strlen(directory) + 16 <= MAX_BASEDIR_LENGTH);
	char *temp = evaluateRelativePathName(directory, "index.long");
	strcpy(baseDirectory, temp);
	free(temp);
	mkdir(baseDirectory, DEFAULT_DIRECTORY_PERMISSIONS);

	termCount = 0;
	byteSize = 0;
	postingCount = 0;
	listBeingUpdated = NULL;
	fileUpdateCnt = 0;

	// process all terms in the index in order to get termCount and byteSize
	// aggregate values
	DIR *d = opendir(baseDirectory);
	if (d == NULL) {
		sprintf(errorMessage, "Unable to open directory: %s", baseDirectory);
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
	assert(d != NULL);
	struct dirent *entry;
	while ((entry = readdir(d)) != NULL) {
		if ((entry->d_name[0] == '.') || (entry->d_name[0] == 0))
			continue;
		if (strlen(entry->d_name) > MAX_TOKEN_LENGTH)
			continue;
		struct stat buf;
		char fileName[MAX_FILEPATH_LENGTH + 1];
		sprintf(fileName, "%s/%s", baseDirectory, entry->d_name);
		int status = stat(fileName, &buf);
		assert(status == 0);
		if (!S_ISREG(buf.st_mode))
			continue;
		termCount++;
		byteSize += buf.st_size;
		PostingListInFile plif(fileName);
		postingCount += plif.getPostingCount();
	}
	closedir(d);

	// load terms and appearance maps from disk
	loadTermMap();
	
	sprintf(errorMessage,
			"Opening in-place index: %d terms, %lld postings, %lld bytes",
			termCount, static_cast<long long>(postingCount), static_cast<long long>(byteSize));
	log(LOG_DEBUG, LOG_ID, errorMessage);
} // end of FS_InPlaceIndex(Index*, char*)


FS_InPlaceIndex::~FS_InPlaceIndex() {
	LocalLock lock(this);
	getPostingListInFile(NULL);
	saveTermMap();
	sprintf(errorMessage, "Closing in-place index: %d terms, %lld postings, %lld bytes",
			termCount, static_cast<long long>(postingCount), static_cast<long long>(byteSize));
	log(LOG_DEBUG, LOG_ID, errorMessage);
	sprintf(errorMessage, "Total number of file updates performed: %lld.", fileUpdateCnt);
	log(LOG_DEBUG, LOG_ID, errorMessage);
} // end of ~FS_InPlaceIndex()


void FS_InPlaceIndex::addPostings(const char *term, offset *postings, int count) {
	LocalLock lock(this);
	getPostingListInFile(term)->addPostings(postings, count);
	if (termMap->find(term) == termMap->end())
		addNewTerm(term);
	postingCount += count;
} // end of addPostings(char*, offset*, int)


void FS_InPlaceIndex::addPostings(
		const char *term, byte *compressedPostings, int byteLength,
		int count, offset first, offset last) {
	LocalLock lock(this);
	getPostingListInFile(term)->addPostings(compressedPostings, byteLength, count, first, last);
	if (termMap->find(term) == termMap->end())
		addNewTerm(term);
	postingCount += count;
} // end of addPostings(char*, byte*, int, int, offset, offset)


void FS_InPlaceIndex::addNewTerm(const char *term) {
	InPlaceTermDescriptor td;
	strcpy(td.term, term);
	td.appearsInIndex = 0;
	td.extra = NULL;
	(*termMap)[term] = td;
	termCount++;
} // end of addNewTerm(char*)


ExtentList * FS_InPlaceIndex::getPostings(const char *term) {
	LocalLock lock(this);
	getPostingListInFile(NULL);
	char fileName[MAX_FILEPATH_LENGTH + 1];
	getFilePathForTerm(term, fileName);
	if (!fileExists(fileName))
		return new ExtentList_Empty();
	else
		return PostingListInFile(fileName).getPostings(-1);
} // end of getPostings(char*)


void FS_InPlaceIndex::getFilePathForTerm(const char *term, char *filePath) {
	sprintf(filePath, "%s/%s", baseDirectory, term);
	replaceChar(&filePath[strlen(baseDirectory) + 1], '/', '_', true);
} // end of getFilePathForTerm(char*, char*)


PostingListInFile * FS_InPlaceIndex::getPostingListInFile(const char *term) {
	LocalLock lock(this);
	if (term == NULL) {
		if (listBeingUpdated != NULL) {
			byteSize += listBeingUpdated->getFileSize() - currentListsOriginalSize;
			delete listBeingUpdated;
			listBeingUpdated = NULL;
		}
		return NULL;
	}
	else {
		char fileName[MAX_FILEPATH_LENGTH + 1];
		getFilePathForTerm(term, fileName);
		if (listBeingUpdated != NULL) {
			char *fn = listBeingUpdated->getFileName();
			if (strcmp(fileName, fn) == 0)
				return listBeingUpdated;
			else {
				byteSize += listBeingUpdated->getFileSize() - currentListsOriginalSize;
				delete listBeingUpdated;
			}
		}
		listBeingUpdated = new PostingListInFile(fileName);
		currentListsOriginalSize = listBeingUpdated->getFileSize();
		fileUpdateCnt++;
		return listBeingUpdated;
	}
} // end of getPostingListInFile(char*)


int64_t FS_InPlaceIndex::getTermCount() {
	return termCount;
}


int64_t FS_InPlaceIndex::getByteSize() {
	return byteSize;
}


int64_t FS_InPlaceIndex::getPostingCount() {
	return postingCount;
}


char * FS_InPlaceIndex::getFileName() {
	return evaluateRelativePathName(directory, "index.long");
}


void FS_InPlaceIndex::finishUpdate() {
}


