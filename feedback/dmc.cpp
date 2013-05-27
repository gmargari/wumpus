/**
 * Implementation of the class DMC.
 *
 * author: Stefan Buettcher
 * created: 2006-06-25
 * changed: 2006-06-25
 **/


#include <math.h>
#include <string.h>
#include "dmc.h"
#include "../index/index.h"
#include "../misc/all.h"
#include "../query/getquery.h"


#define LOG_ID "DMC"


DMC::DMC() {
	pinit();
} // end of DMC()


DMC::~DMC() {
	free(nodebuf);
} // end of ~DMC()


void DMC::addToModel(char *text, int length) {
	for (int i = 0; i < length; i += MAX_TEXT_LENGTH) {
		int chunkSize = length - i;
		if (chunkSize > MAX_TEXT_LENGTH)
			chunkSize = MAX_TEXT_LENGTH;
		pdo(&text[i], chunkSize);
	}
	char msg[256];
	sprintf(msg, "Adding %d bytes to model. Nodes used: %d.", length, nodeCnt);
	log(LOG_DEBUG, LOG_ID, msg);
} // end of addToModel(char*, int)


void DMC::addToModel(Index *index, offset start, offset end, bool filtered) {
	int length;
	char *text = getText(index, start, end, filtered, &length);
	addToModel(text, length);
	free(text);
} // end of addToModel(Index*, offset, offset, bool)


double DMC::getScore(char *text, int length) {
	double result = 0;
	for (int i = 0; i < length; i += MAX_TEXT_LENGTH) {
		int chunkSize = length - i;
		if (chunkSize > MAX_TEXT_LENGTH)
			chunkSize = MAX_TEXT_LENGTH;
		result += pdo(&text[i], chunkSize) * chunkSize;
		pundo();
	}
	char msg[256];
	sprintf(msg, "Scoring %d bytes. Nodes used: %d.", length, nodeCnt);
	log(LOG_DEBUG, LOG_ID, msg);
	return result / length;
} // end of getScore(char*, int)


double DMC::getScore(Index *index, offset start, offset end, bool filtered) {
	int length;
	char *text = getText(index, start, end, filtered, &length);
	double result = getScore(text, length);
	free(text);
	return result;
} // end of getScore(Index*, offset, offset, bool)


char * DMC::getText(Index *index, offset start, offset end, bool filtered, int *length) {
/*	char *FILTERED_MODIFIERS[2] = { "filtered", NULL };
	char startEnd[64];
	sprintf(startEnd, OFFSET_FORMAT " " OFFSET_FORMAT, start, end);
	GetQuery *gq = new GetQuery(index, "get",
			                        filtered ? FILTERED_MODIFIERS : EMPTY_MODIFIERS,
															startEnd, Index::SUPERUSER, -1);
	gq->parse();
	char line[Query::MAX_RESPONSELINE_LENGTH + 1];
	line[0] = 0;
	gq->getNextLine(line);
	delete gq;
	*length = strlen(line);
	return duplicateString(line);
*/
	return NULL;
} // end of getText(Index*, offset, offset, bool, int*)


void DMC::pinit() {
	nodebuf = typed_malloc(DMC_Node, 1);
	nodeCnt = maxNodeCnt = 0;
	navail = NULL;
	pflush();
} // end of pinit(int)


void DMC::pflush() {
	for (int j = 0; j < 256; j++) {
		for (int i = 0; i < 127; i++) {
			nodes[j][i].count[0] = 0.2;
			nodes[j][i].count[1] = 0.2;
			nodes[j][i].next[0] = &nodes[j][2*i + 1];
			nodes[j][i].next[1] = &nodes[j][2*i + 2];
		}
		for (int i = 127; i < 255; i++) {
			nodes[j][i].count[0] = 0.2;
			nodes[j][i].count[1] = 0.2;
			nodes[j][i].next[0] = &nodes[i+1][0];
			nodes[j][i].next[1] = &nodes[i-127][0];
		}
	}
} // end of pflush()


void DMC::preset() {
	p = &nodes[0][0];
	pr = preserve;
} // end of preset()


double DMC::predict() {
	double r = p->count[0] / (p->count[0] + p->count[1]);
	assert(r >= 0 && r <= 1);
	if (r < 0.000001)
		return 0.000001;
	if (r > 0.999999)
		return 0.999999;
	return r;
} // end of predict()


int DMC::pupdate(int b) {
	double r;
	pr->oldp = p;
	pr->old = *p;
	pr->nextp = p->next[b];
	pr->next = *p->next[b];
	if ((p->count[b] >= 2) &&
	    (p->next[b]->count[0] + p->next[b]->count[1] >= 2 + p->count[b])) {
		newnode = (pr++)->newp = getNewNode();
		if (newnode == NULL)
			return -1;
		r = p->count[b] / (p->next[b]->count[1] + p->next[b]->count[0]);
		p->next[b]->count[0] -=
			newnode->count[0] = p->next[b]->count[0] * r;
		p->next[b]->count[1] -=
			newnode->count[1] = p->next[b]->count[1] * r;
		newnode->next[0] = p->next[b]->next[0];
		newnode->next[1] = p->next[b]->next[1];
		p->next[b] = newnode;
	}
	else
		(pr++)->newp = NULL;
	p->count[b]++;
	p = p->next[b];
	return 0;
} // end of pupdate(int)


void DMC::pfreeze() {
	pf = pr;
} // end of pfreeze()


void DMC::pundo() {
	for (pr = pf - 1; pr >= preserve; pr--) {
		*pr->oldp = pr->old;
		*pr->nextp = pr->next;
		if (pr->newp != NULL) {
			pr->newp->next[0] = navail;
			navail = pr->newp;
			nodeCnt--;
		}
	}
} // end of pundo()


double DMC::pdo(char *text, int length) {
	if (length <= 0)
		return 0;
	assert(length <= MAX_TEXT_LENGTH);
	double s = 0;
	preset();
	for (int j = 0; j < length; j++) {
		double r = 1;
		for (int i = 0; i < 8; i++) {
			int bit = !!(text[j] & (1 << i));
			if (bit)
				r *= 1 - predict();
			else
				r *= predict();
			if (pupdate(bit)) {
				pfreeze();
				return -999;
			}
		}
		s -= log(r);
	}
	pfreeze();
	return s / log(2) / length;
} // end of pdo(char*, int)


DMC_Node * DMC::getNewNode() {
	if (maxNodeCnt == 0) {
		maxNodeCnt = MAX_NODE_COUNT;
		nodebuf = typed_realloc(DMC_Node, nodebuf, maxNodeCnt);
		for (int i = nodeCnt; i < maxNodeCnt; i++)
			nodebuf[i].next[0] = &nodebuf[i + 1];
		nodebuf[maxNodeCnt - 1].next[0] = NULL;
		navail = &nodebuf[nodeCnt];
	}
	if (nodeCnt >= maxNodeCnt)
		return NULL;
	DMC_Node *result = navail;
	navail = navail->next[0];
	nodeCnt++;
	return result;
} // end of getNewNode()


