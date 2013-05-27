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
 * The TRECMultiInputStream class is used to combine data from several TREC
 * input streams into one big stream. It is assumed that the ordering on the
 * input documents is the same for all input streams.
 *
 * The format of a descriptor file defining multiple input streams is:
 *
 *   <TREC_MULTIPLE_INPUT_STREAM>
 *     Stream: FILE1 FILE2 ... FILE5
 *     Stream: FILE1 FILE2 ... FILE7
 *   </TREC_MULTIPLE_INPUT_STREAM>
 *
 * author: Stefan Buettcher
 * created: 2005-07-21
 * changed: 2005-07-21
 **/


#ifndef __FILTERS__TRECMULTI_H
#define __FILTERS__TRECMULTI_H


#include "trec_inputstream.h"
#include "../misc/stringtokenizer.h"


class TRECMultiInputStream : public TRECInputStream {

public:

	static const int MAX_STREAM_COUNT = 8;

	static const int LINE_LENGTH = 65536;

protected:

	pid_t childProcess;

	char line[8192];

	/** Number of individual input streams to be merged. **/
	int streamCount;

	/** Names of all input files in a given stream. **/
	StringTokenizer *fileNames[MAX_STREAM_COUNT];

	FILE *currentFile[MAX_STREAM_COUNT];

	char nextLine[MAX_STREAM_COUNT][LINE_LENGTH];

	char currentDocument[LINE_LENGTH];

	char nextDocument[MAX_STREAM_COUNT][LINE_LENGTH];

public:

	TRECMultiInputStream(const char *fileName);

	virtual ~TRECMultiInputStream();

	virtual int getDocumentType();

	static bool canProcess(const char *fileName, byte *fileStart, int length);

private:

	/**
	 * This method reads data from the input streams, combines them, and writes
	 * the resulting data to the file given by "outFD".
	 **/
	void processInputStreams(int outFD);

	bool getNextLine(int whichStream, char *line, int maxLen);

}; // end of class TRECMultiInputStream


#endif


