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
 * Implementation of the Annotator class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-22
 * changed: 2009-02-01
 **/


#include <string.h>
#include "annotator.h"
#include "../misc/all.h"


static const char *ANNOTATOR_WORKFILE = "annotations";

static const char * LOG_ID = "Annotator";


Annotator::Annotator(const char *workDirectory, bool create) {
	char *fileName = (char*)malloc(strlen(workDirectory) + 32);
	strcpy(fileName, workDirectory);
	if (fileName[strlen(fileName) - 1] != '/')
		strcat(fileName, "/");
	strcat(fileName, ANNOTATOR_WORKFILE);
	if (create) {
		// create new Annotator data
		annotatorData = new FileSystem(fileName, FS_PAGESIZE, FS_PAGECOUNT);
		if (!annotatorData->isActive()) {
			char msg[256];
			snprintf(msg, sizeof(msg), "Unable to create %s\n", fileName);
			log(LOG_ERROR, LOG_ID, msg);
			exit(1);
		}
		for (int i = 0; i < HASHTABLE_SIZE; i++) {
			File *f = new File(annotatorData, i, true);
			delete f;
		}
		annotatorData->flushCache();
	}
	else {
		// resume from existent data
		annotatorData = new FileSystem(fileName);
		if (!annotatorData->isActive()) {
			char msg[256];
			snprintf(msg, sizeof(msg), "Unable to open %s\n", fileName);
			log(LOG_ERROR, LOG_ID, msg);
			exit(1);
		}
	}
	free(fileName);
} // end of Annotator(char*, bool)


Annotator::~Annotator() {
	delete annotatorData;
} // end of Annotator()


void Annotator::addAnnotation(offset position, const char *annotation) {
	// lock data and read stuff from file
	LocalLock lock(this);

	int hashValue = position % HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	File *f = new File(annotatorData, hashValue, false);
	int annotationsInFile = f->getSize() / sizeof(Annotation);
	Annotation *annotations =
		(Annotation*)malloc((annotationsInFile + 1) * sizeof(Annotation));
	f->read(annotationsInFile * sizeof(Annotation), annotations);

	for (int i = 0; i < annotationsInFile; i++) {
		if (annotations[i].position == position) {
			f->seek(i * sizeof(Annotation));
			memset(annotations[i].annotation, 0, MAX_ANNOTATION_LENGTH + 1);
			strncpy(annotations[i].annotation, annotation, MAX_ANNOTATION_LENGTH);
			f->write(sizeof(Annotation), &annotations[i]);
			free(annotations);
			delete f;
			return;
		}
	}

	annotations[annotationsInFile].position = position;
	memset(annotations[annotationsInFile].annotation, 0, MAX_ANNOTATION_LENGTH + 1);
	strncpy(annotations[annotationsInFile].annotation, annotation, MAX_ANNOTATION_LENGTH);
	f->write(sizeof(Annotation), &annotations[annotationsInFile]);
	free(annotations);
	delete f;
} // end of addAnnotation(offset, char*)


void Annotator::getAnnotation(offset position, char *buffer) {
	// lock data and read stuff from file
	LocalLock lock(this);

	int hashValue = position % HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	File *f = new File(annotatorData, hashValue, false);
	int annotationsInFile = f->getSize() / sizeof(Annotation);
	Annotation *annotations =
		(Annotation*)malloc(annotationsInFile * sizeof(Annotation));
	f->read(annotationsInFile * sizeof(Annotation), annotations);
	delete f;

	for (int i = 0; i < annotationsInFile; i++)
		if (annotations[i].position == position) {
			strcpy(buffer, annotations[i].annotation);
			free(annotations);
			return;
		}
	free(annotations);
	buffer[0] = 0;
} // end of getAnnotation(offset, char*)


void Annotator::removeAnnotation(offset position) {
	// lock data and read stuff from file
	LocalLock lock(this);

	int hashValue = position % HASHTABLE_SIZE;
	if (hashValue < 0)
		hashValue = -hashValue;
	File *f = new File(annotatorData, hashValue, false);
	int annotationsInFile = f->getSize() / sizeof(Annotation);
	Annotation *annotations =
		(Annotation*)malloc(annotationsInFile * sizeof(Annotation));
	f->read(annotationsInFile * sizeof(Annotation), annotations);
	delete f;

	for (int i = 0; i < annotationsInFile; i++)
		if (annotations[i].position == position) {
			annotatorData->deleteFile(hashValue);
			annotationsInFile--;
			if (i < annotationsInFile)
				memcpy(&annotations[i], &annotations[annotationsInFile], sizeof(Annotation));
			f = new File(annotatorData, hashValue, true);
			f->write(annotationsInFile * sizeof(Annotation), annotations);
			delete f;
			free(annotations);
			return;
		}
	free(annotations);
} // end of removeAnnotation(offset)



