/**
 * Definition of the DMC class. DMC ("dynamic markov compression") is a
 * data compression algorithm devised by Cormack and Horspool. A description
 * can be found in:
 *
 *   Cormack and Horspool. "Data Compression using Dynamic Markov Modelling"
 *   The Computer Journal 30:6, December 1987.
 *
 * We use DMC to compute the similarity between two chunks of text, which is
 * used in search result reranking.
 *
 * author: Stefan Buettcher
 * created: 2006-06-25
 * changed: 2006-06-25
 **/


#ifndef __FEEDBACK_DMC_H
#define __FEEDBACK_DMC_H


#include "../index/index_types.h"
#include "../misc/all.h"


class Index;


struct DMC_Node {
	float count[2];
	DMC_Node *next[2];
};


struct DMC_PPP {
	DMC_Node old, next;
	DMC_Node *oldp, *nextp;
	DMC_Node *newp;
};


class DMC {

public:

	/** How much memory to allocate for the prediction table? **/
	static const int MAX_NODE_COUNT = 20000000;

	/** Maximum number of bytes we can compress at a time. **/
	static const int MAX_TEXT_LENGTH = 2 * 1024 * 1024;

private:

	DMC_Node *p, *newnode, nodes[256][256], *nodebuf, *navail;

	DMC_PPP preserve[MAX_TEXT_LENGTH * 2 * 8 + 64], *pr, *pf;

	int nodeCnt, maxNodeCnt;

public:

	/** Creates a DMC coder with empty prediction model. **/
	DMC();

	~DMC();

	/** Adds the given piece of text to the compression model. **/
	void addToModel(char *text, int length);

	/**
	 * Executes an @get query to fetch the text associated with the given
	 * start and end offsets. The parameter "filtered" is used to tell the
	 * method whether it should construct the GetQuery object with the
	 * additional modifier "[filtered]" or not.
	 **/
	void addToModel(Index *index, offset start, offset end, bool filtered);

	/**
	 * Returns a score for the given piece of text that tell us how close
	 * it is to the compression model.
	 **/
	double getScore(char *text, int length);

	/** Alternative to the above getScore method. **/
	double getScore(Index *index, offset start, offset end, bool filtered);

private:

	/**
	 * Returns the text associated with thr given index extent for the given
	 * index. Memory has to be freed by the caller.
	 **/
	char *getText(Index *index, offset start, offset end, bool filtered, int *length);

private:

	void pinit();

	void pflush();

	void preset();

	double predict();

	int pupdate(int b);

	void pfreeze();

	void pundo();

	double pdo(char *text, int length);

	DMC_Node *getNewNode();
	
}; // end of class DMC


#endif


