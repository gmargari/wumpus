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


#include <assert.h>
#include <stdio.h>
#include "qrels.h"
#include "../misc/all.h"


static const char *LOG_ID = "Qrels";


Qrels::Qrels(const string& filename) {
	FILE *f = fopen(filename.c_str(), "r");
	if (f == NULL) {
		log(LOG_ERROR, LOG_ID, string("Unable to open qrels file: ") + filename);
	}
	char line[1024];
	while (fgets(line, sizeof(line), f) != NULL) {
		char topic[1024], dummy[1024], docid[1024];
		int judgement;
		if (sscanf(line, "%s%s%s%d", topic, dummy, docid, &judgement) == 4) {
			qrels[topic][docid] = judgement;
		}
	}
	fclose(f);
}


Qrels::~Qrels() {
}


void Qrels::getQrels(map<string, map<string, int> >* qrels) {
	*qrels = this->qrels;
}


void Qrels::getRelevantDocuments(const string& topic, vector<string>* docids) {
	// Initialize the output parameter.
	docids->clear();

	if (qrels.find(topic) == qrels.end()) {
		log(LOG_ERROR, LOG_ID, string("No qrels for topic: ") + topic);
		return;
	}

	const map<string, int>& qrels_for_topic = qrels[topic];
	for (map<string, int>::const_iterator iter = qrels_for_topic.begin();
	     iter != qrels_for_topic.end(); ++iter) {
		const string& docid = iter->first;
		const int judgement = iter->second;
		if (judgement > 0)
			docids->push_back(docid);
	}
}


void Qrels::getNonRelevantDocuments(const string& topic, vector<string>* docids) {
	// Initialize the output parameter.
	docids->clear();

	if (qrels.find(topic) == qrels.end())
		return;

	const map<string, int>& qrels_for_topic = qrels[topic];
	for (map<string, int>::const_iterator iter = qrels_for_topic.begin();
	     iter != qrels_for_topic.end(); ++iter) {
		const string& docid = iter->first;
		const int judgement = iter->second;
		if (judgement == 0)
			docids->push_back(docid);
	}
}

