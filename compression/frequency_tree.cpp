#include <assert.h>
#include <string.h>
#include "frequency_tree.h"


FrequencyTree::FrequencyTree(int min, int max) {
	assert(max > min);
	treeSize = 2;
	while (treeSize <= max - min)
		treeSize += treeSize;
	tree = new int[2 * treeSize];
	memset(tree, 0, 2 * treeSize * sizeof(int));
	this->min = min;
	this->max = max;
} // end of FrequencyTree(unsigned int, unsigned int)


FrequencyTree::~FrequencyTree() {
	delete[] tree;
} // end of ~FrequencyTree()


void FrequencyTree::reset(int value) {
	// reset all leaf nodes
	for (int i = 0; i < treeSize; i++)
		if (i > max - min)
			tree[treeSize + i] = 0;
		else
			tree[treeSize + i] = value;

	// work you way up the tree, computing the sums of the children's frequencies
	// in each node of the tree
	for (int i = treeSize - 1; i > 0; i--)
		tree[i] = tree[i * 2] + tree[i * 2 + 1];
} // end of reset(int)


void FrequencyTree::increaseFrequency(int node, int delta) {
	if ((node < min) || (node > max))
		return;
	node = node - min + treeSize;
	while (node > 0) {
		tree[node] += delta;
		node = (node >> 1);
	}
} // end of increaseFrequency(int)


int FrequencyTree::getFrequency(int node) {
	if ((node < min) || (node > max))
		return 0;
	node = node - min + treeSize;
	return tree[node];
} // end of getFrequency(int)


int FrequencyTree::getCumulativeFrequency(int node) {
	if (node < min)
		return 0;
	if (node > max)
		return tree[1];
	node = node - min + treeSize;
	int result = tree[node];
	while (node > 0) {
		if (node & 1) {
			// right-hand child: add value of left-hand child
			result += tree[node - 1];
		}
		else {
			// left-hand child: do not add value of right-hand child
		}
		node = (node >> 1);
	}
	return result;
} // end of getCumulativeFrequency(int)


int FrequencyTree::getTotalFrequency() {
	return tree[1];
} // end of getTotalFrequency()


