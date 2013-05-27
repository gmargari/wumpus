/**
 * author: Stefan Buettcher
 * created: 2007-02-11
 * changed: 2009-02-01
 **/


#include <stdio.h>
#include <string.h>
#include "inplace_index.h"
#include "fs_inplace_index.h"
#include "my_inplace_index.h"
#include "../misc/all.h"


static const char *LOG_ID = "InPlaceIndex";


InPlaceIndex::InPlaceIndex() {
	termMap = new std::map<std::string,InPlaceTermDescriptor>();
	directory = NULL;
} // end of InPlaceIndex()


InPlaceIndex::~InPlaceIndex() {
	if (termMap != NULL) {
		delete termMap;
		termMap = NULL;
	}
	if (directory != NULL) {
		free(directory);
		directory = NULL;
	}
} // end of ~InPlaceIndex()


void InPlaceIndex::loadTermMap() {
	if (directory == NULL)
		log(LOG_ERROR, LOG_ID, "directory == NULL. Forgot to set?");
	assert(directory != NULL);
	char *fileName = evaluateRelativePathName(directory, "index.long.list");
	FILE *f = fopen(fileName, "r");
	free(fileName);
	if (f == NULL)
		log(LOG_DEBUG, LOG_ID, "In-place index term map file does not exist. Assuming index is empty.");
	else {
		char line[256];
		while (fgets(line, sizeof(line), f) != NULL) {
			if (strchr(line, '\n') == NULL) {
				sprintf(errorMessage, "Broken term map file: %s", line);
				log(LOG_ERROR, LOG_ID, errorMessage);
				break;
			}
			char term[256], dummy[256];
			unsigned int flags;
			if (sscanf(line, "%s%u%s", term, &flags, dummy) != 2) {
				sprintf(errorMessage, "Broken term map file: %s", line);
				log(LOG_ERROR, LOG_ID, errorMessage);
				break;
			}
			InPlaceTermDescriptor td;
			strcpy(td.term, term);
			td.appearsInIndex = flags;
			td.extra = NULL;
			(*termMap)[term] = td;
		}
		fclose(f);
	}
} // end of loadTermMap(char*)


void InPlaceIndex::saveTermMap() {
	if (directory == NULL)
		log(LOG_ERROR, LOG_ID, "directory == NULL. Forgot to set?");
	assert(directory != NULL);
	char *fileName = evaluateRelativePathName(directory, "index.long.list");
	FILE *f = fopen(fileName, "w");
	free(fileName);
	if (f == NULL) {
		sprintf(errorMessage, "Unable to create file: %s", fileName);
		log(LOG_ERROR, LOG_ID, errorMessage);
	}
	else {
		std::map<std::string,InPlaceTermDescriptor>::iterator iter;
		for (iter = termMap->begin(); iter != termMap->end(); ++iter)
			fprintf(f, "%s %u\n", iter->second.term, iter->second.appearsInIndex);
		fclose(f);
	}
} // end of saveTermMap(char*)


InPlaceIndex * InPlaceIndex::getIndex(Index *owner, const char *directory) {
//	return new FS_InPlaceIndex(owner, directory);
	return new MyInPlaceIndex(owner, directory);
}


InPlaceTermDescriptor * InPlaceIndex::getDescriptor(const char *term) {
	std::map<std::string,InPlaceTermDescriptor>::iterator iter = termMap->find(term);
	if (iter == termMap->end())
		return NULL;
	else
		return &(iter->second);
} // end of getDescriptor(char*)


char * InPlaceIndex::getTermSequence() {
	int allocated = 8192, used = 0;
	char *result = (char*)malloc(allocated);
	std::map<std::string,InPlaceTermDescriptor>::iterator iter;
	for (iter = termMap->begin(); iter != termMap->end(); ++iter) {
		int len = iter->first.length();
		if (used + len > allocated - 4) {
			allocated = MAX(allocated + 8192, (int)(allocated * 1.21));
			result = (char*)realloc(result, allocated);
		}
		strcpy(&result[used], iter->first.c_str());
		used += len + 1;
	}
	result[used++] = 0;
	result = (char*)realloc(result, used);
	return result;
} // end of getTermSequence()


