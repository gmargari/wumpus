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
 * Implementation of the QueryTokenizer class.
 * 
 * author: Stefan Buettcher
 * created: 2005-04-14
 * changed: 2005-04-14
 **/


#include <string.h>
#include "querytokenizer.h"
#include "../misc/all.h"


typedef unsigned char byte;


QueryTokenizer::QueryTokenizer(const char *arguments) {
	sequence = duplicateString(arguments);
	inputPosition = 0;
	if (sequence == NULL)
		inputLength = 0;
	else {
		byte *b = (byte*)sequence;
		inputLength = strlen(sequence);
		if ((inputLength == 1) && (b[0] <= 32))
			inputLength = 0;
		else {
			while ((inputLength > 1) && (b[inputLength - 1] <= 32))
				inputLength--;
			while ((sequence[inputPosition] > 0) && (sequence[inputPosition] <= 32))
				inputPosition++;
		}
	}
} // end of QueryTokenizer(char*)


QueryTokenizer::~QueryTokenizer() {
	if (sequence != NULL)
		free(sequence);
} // end of ~QueryTokenizer()


char * QueryTokenizer::getNext() {
	if (inputPosition >= inputLength)
		return NULL;
	while ((sequence[inputPosition] > 0) && (sequence[inputPosition] <= 32))
		inputPosition++;
	char *result = &sequence[inputPosition];
	bool inQuotes = false;
	while ((inQuotes) || (sequence[inputPosition] != ',')) {
		if (sequence[inputPosition] == 0)
			break;
		if (sequence[inputPosition] == '"')
			inQuotes = !inQuotes;
		inputPosition++;
	}
	sequence[inputPosition++] = 0;
	return result;
} // end of getNext()


bool QueryTokenizer::hasNext() {
	if (inputPosition >= inputLength)
		return false;
	else
		return true;
} // end of hasNext()


int QueryTokenizer::getTokenCount() {
	int oldInputPosition = inputPosition;
	char *oldSequence = duplicateString(sequence);
	int result = 0;
	while (getNext() != NULL)
		result++;
	inputPosition = oldInputPosition;
	free(sequence);
	sequence = oldSequence;
	return result;
} // end of getTokenCount()


