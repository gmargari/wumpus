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
 * CompressedInputStream is an abstract class used to realize on-the-fly
 * input file decompression. Implementations are GZIPInputStream and
 * BZIP2InputStream.
 *
 * On-the-fly decompression is realized by creating a child process that starts
 * an external command performing the actual decompression. Communication
 * between child and parent is done through a pipe.
 *
 * author: Stefan Buettcher
 * created: 2005-07-22
 * changed: 2005-07-22
 **/


#ifndef __FILTERS__COMPRESSED_H
#define __FILTERS__COMPRESSED_H


#include "inputstream.h"
#include <sys/types.h>


class CompressedInputStream : public FilteredInputStream {

protected:

	/** PID of the process that is responsible for the decompression. **/
	pid_t childProcess;

	/** This guy reads the uncompressed data and returns a stream of tokens. **/
	FilteredInputStream *uncompressedStream;

	/** File name of the input file. **/
	char *fileName;

	/**
	 * Command to be used for decompressing the input. This value is set by
	 * the implementing class (e.g., GZIPInputStream).
	 **/
	char *decompressionCommand;

public:

	/** Default constructor. Initializes member variables to 0. **/
	CompressedInputStream();

	/** The constructor kills the child process and waits for its termination. **/
	~CompressedInputStream();

	/** Returns the document type ID of the data inside the compressed archive. **/
	virtual int getDocumentType();

	/** Standard getNextToken implementation, run on the decompressed input data. **/
	virtual bool getNextToken(InputToken *result);

	/** Gets the "bufferSize" previous characters from the child's input buffer. **/
	virtual void getPreviousChars(char *buffer, int bufferSize);

protected:

	/**
	 * Creates a child process reading data from the input file and decompressing
	 * it. Creates a FilteredInputStream instance ("uncompressedStream"), depending
	 * on the data found inside the input file.
	 **/
	void initialize();

}; // end of class CompressedInputStream


#endif


