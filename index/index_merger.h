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
 * author: Stefan Buettcher
 * created: 2005-04-24
 * changed: 2007-03-28
 **/


#ifndef __INDEX__INDEX_MERGER_H
#define __INDEX__INDEX_MERGER_H


#include "index.h"
#include "../extentlist/extentlist.h"


class InPlaceIndex;
class OnDiskIndex;


class IndexMerger {

public:

	static const int MULTIPLE_ITERATOR_THRESHOLD = 10;

	static const int ITERATORS_PER_MULTIPLE_ITERATOR = 5;

public:

	/**
	 * Merges the indices given by "iterators". The IndexIterator instances given
	 * will be owned by the function. When the function returns, the "iterators"
	 * array has already been freed.
	 **/
	static void mergeIndices(Index *index,
			char *outputFile, IndexIterator **iterators, int iteratorCount);

	/** Same as above, but with built-in garbage collection ("visible"). **/
	static void mergeIndicesWithGarbageCollection(Index *index,
			char *outputFile, IndexIterator **iterators, int iteratorCount, ExtentList *visible);

	/**
	 * Merges the data found in the input iterators into the given target index,
	 * performing on-the-fly garbage collection if desired.
	 **/
	static void mergeIndices(Index *index,
			OnDiskIndex *target, IndexIterator *input, ExtentList *visible, bool lowPriority);

	/**
	 * Merges the data found in the input iterators into the given target index.
	 * Every list that is encountered during this process and that contains more
	 * than "longListThreshold" postings is not put into the main target index,
	 * but into "longListTarget" instead.
	 **/
	static void mergeWithLongTarget(Index *index,
			OnDiskIndex *target, IndexIterator* input, InPlaceIndex *longListTarget,
			int longListThreshold, bool mayAddNewTermsToLong, int newFlag);

private:

	/**
	 * This method takes a list of postings ("postings") and a list of intervals
	 * ("intervalStart", "intervalEnd") and removes all postings from the list that
	 * do not lie in one of the intervals.
	 * Precondition: Both lists (postings and intervals) are sorted in increasing order.
	 **/
	static int filterPostingsAgainstIntervals(offset *postings, int listLength,
			offset *intervalStart, offset *intervalEnd, int intervalCount);

}; // end of class IndexMerger


#endif


