#ifndef __COMPRESSION__FREQUENCY_TREE_H
#define __COMPRESSION__FREQUENCY_TREE_H


class FrequencyTree {

private:

	/** Minimum and maximum nodes in tree. **/
	int min, max;

	/** Implicit tree holding frequency information. **/
	int *tree;
	
	/** Number of leaf nodes in the tree. **/
	int treeSize;

public:

	/**
	 * Creates a new FrequencyTree that holds frequency information for items
	 * numbered "min" through "max".
	 **/
	FrequencyTree(int min, int max);

	/** Deletes the tree. **/
	~FrequencyTree();

	/** Resets all nodes' frequency value to the given value. **/
	void reset(int value);

	/** Increases the frequency for the given node by "delta". **/
	void increaseFrequency(int node, int delta);

	/** Returns the frequency value for the given node. **/
	int getFrequency(int node);

	/**
	 * Returns the sum of the frequency values of all nodes smaller than the
	 * given node (including the given node itself).
	 **/
	int getCumulativeFrequency(int node);

	/** Returns the sum of all frequency values in the tree. **/
	int getTotalFrequency();

}; // end of class FrequncyTree


#endif


