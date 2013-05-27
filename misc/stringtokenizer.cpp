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
 * created: 2004-10-08
 * changed: 2004-10-20
 **/


#include <stdio.h>
#include <string.h>
#include "stringtokenizer.h"
#include "../misc/alloc.h"


StringTokenizer::StringTokenizer(const char *string, const char *delim) {
	this->string = (char*)malloc(strlen(string) + 2);
	strcpy(this->string, string);
	this->delim = (char*)malloc(strlen(delim) + 2);
	strcpy(this->delim, delim);
	nextPosition = 0;
	stringLength = strlen(string);
} // end of StringTokenizer(const char*, const char*)


StringTokenizer::~StringTokenizer() {
	free(string);
	free(delim);
} // end of ~StringTokenizer()


bool StringTokenizer::hasNext() {
	if (string[nextPosition] == 0)
		return false;
	else
		return true;
} // end of hasNext()


char * StringTokenizer::nextToken() {
	return getNext();
}


char * StringTokenizer::getNext() {
	if (nextPosition >= stringLength)
		return NULL;
	int pos = nextPosition;
	while (string[pos] != 0) {
		bool found = false;
		for (int i = 0; delim[i] != 0; i++)
			if (string[pos] == delim[i])
				found = true;
		if (found)
			break;
		pos++;
	}
	if (string[pos] == 0)
		string[pos + 1] = 0;
	else
		string[pos] = 0;
	char *result = &string[nextPosition];
	nextPosition = pos + 1;
	return result;
} // end of getNext()


void StringTokenizer::split(const std::string &s, const std::string &delim,
                            std::vector<std::string> *v) {
	v->clear();
	StringTokenizer tokenizer(s.c_str(), delim.c_str());
	for (char *token = tokenizer.getNext(); token != NULL; token = tokenizer.getNext())
		v->push_back(token);
}


std::string StringTokenizer::join(const std::vector<std::string> &v,
                             const std::string &delim) {
	if (v.size() == 0)
		return "";
	int size = 0;
	for (int i = 0; i < v.size(); ++i)
		size += v[i].size() + delim.size();
	std::string result = v[0];
	result.reserve(size);
	for (int i = 1; i < v.size(); ++i)
		result += delim + v[i];
	return result;
}


