/**
 * Copyright (C) 2008 Stefan Buettcher. All rights reserved.
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
 * created: 2008-12-25
 * changed: 2008-12-25
 **/


#ifndef __QRELS__QRELS_H
#define __QRELS__QRELS_H


#include <map>
#include <string>
#include <vector>

using namespace std;

class Qrels {
public:
	Qrels(const string& filename);

	~Qrels();

	void getQrels(map<string, map<string, int> >* qrels);

	void getRelevantDocuments(const string& topic, vector<string>* docids);

	void getNonRelevantDocuments(const string& topic, vector<string>* docids);

private:
	map<string, map<string, int> > qrels;
};  // end of class Qrels


#endif


