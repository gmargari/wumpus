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
 * Class definition of the Annotator class. Annotator is used to store
 * annotation for index ranges. Annotations can be used to speed up document
 * ID requests, as performed when running TREC stuff.
 * Annotations can be inserted by using the @annotate command:
 *   @annotate INDEX_POSITION ANNOTATIONS
 * They can be read later on by using the @getannotation command:
 *   @getannotation INDEX_POSITION
 * The index server will return the annotation at the given index position or
 * an empty string if there is no annotation for the index position.
 *
 * author: Stefan Buettcher
 * created: 2004-11-22
 * changed: 2004-11-24
 **/


#ifndef __INDEX_TOOLS__ANNOTATOR_H
#define __INDEX_TOOLS__ANNOTATOR_H


#include "../index/index_types.h"
#include "../filesystem/filesystem.h"
#include "../misc/all.h"


#define MAX_ANNOTATION_LENGTH (31 - sizeof(offset))


typedef struct {
	offset position;
	char annotation[MAX_ANNOTATION_LENGTH + 1];
} Annotation;


class Annotator : public Lockable {

private:

	FileSystem *annotatorData;

	static const int FS_PAGESIZE = 2048;
	static const int FS_PAGECOUNT = 1024;
	static const int HASHTABLE_SIZE = 1021;

public:

	/**
	 * Creates a new Annotator instance from the data found in "workDirectory".
	 * If "create" is true, the Annotator creates its file structure anew.
	 **/
	Annotator(const char *workDirectory, bool create);

	~Annotator();

	void addAnnotation(offset position, const char *annotation);

	/**
	 * Prints the annotation found at position "position" into the buffer given
	 * by "buffer". If no annotation can be found, buffer will equal "\0".
	 **/
	void getAnnotation(offset position, char *buffer);

	void removeAnnotation(offset position);

}; // end of class Annotator


#endif


