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
 * Implementation of the abstract Lexicon class. See lexicon.h for documentation.
 *
 * author: Stefan Buettcher
 * created: 2005-05-15
 * changed: 2007-03-05
 **/


#include <string.h>
#include "lexicon.h"
#include "../misc/all.h"


Lexicon::Lexicon() {
	firstPosting = MAX_OFFSET;
	lastPosting = 0;
}


void Lexicon::getClassName(char *target) {
	strcpy(target, "Lexicon");
}


void Lexicon::setInputStream(FilteredInputStream *fis) {
}


void Lexicon::setIndexRange(offset firstPosting, offset lastPosting) {
	this->firstPosting = firstPosting;
	this->lastPosting = lastPosting;
}


void Lexicon::getIndexRange(offset *firstPosting, offset *lastPosting) {
	*firstPosting = this->firstPosting;
	*lastPosting = this->lastPosting;
}


void Lexicon::extendIndexRange(offset first, offset last) {
	this->firstPosting = MIN(first, this->firstPosting);
	this->lastPosting = MAX(last, this->lastPosting);
}


