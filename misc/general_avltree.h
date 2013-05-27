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
 * Definition of the class GeneralAVLTree. A general AVL tree is just an
 * ordinary AVL tree, but it can hold arbitrary data in its nodes. The
 * Comparator interface is used to compare two node values.
 *
 * author: Stefan Buettcher
 * created: 2004-10-19
 * changed: 2004-10-20
 **/


#ifndef __MISC__GENERAL_AVLTREE_H
#define __MISC__GENERAL_AVLTREE_H


#include <sys/types.h>
#include "all.h"
#include "comparator.h"


/**
 * AVLTreeNode instances are used for representing the nodes of the
 * AVLTree. They have two 32-bit integers (key and value).
 **/
typedef struct {

	/** This is all we can carry: a reference to some data structure. **/
	void *value;
	
	/** The height of this node (used for rebalancing). **/
	int32_t height;

	/** Left and right child nodes (-1 for "non-existent"). **/
	int32_t leftChild, rightChild;

	/** Parent of the node (-1 for "non-existent"). **/
	int32_t parent;

} GeneralAVLTreeNode;


class GeneralAVLTree {

public:

	static const int32_t INITIAL_NODE_COUNT = 1024;

	static const double GROWTH_RATE = 1.25;

private:

	/** Comparator instance that is used to compare node values. **/
	Comparator *comparator;

	/** A simple array of nodes. **/
	GeneralAVLTreeNode *nodes;

	/** Number of nodes in the tree. **/
	int32_t nodeCount;

	/** Number of node slots allocated. **/
	int32_t slotCount;

	/** Node index of the root node. **/
	int32_t root;

	/** A list of free nodes in the array. **/
	int32_t *freeNodes;

	/** Number of free nodes. **/
	int32_t freeNodeCount;

public:

	/**
	 * Creates a new GeneralAVLTree instance that uses the Comparator given
	 * by "comp" to compare node values.
	 **/
	GeneralAVLTree(Comparator *comp);

	/** Deletes the object. **/
	~GeneralAVLTree();

	/**
	 * If there is a node within the tree whose "value" member equals the object
	 * referenced by "key" (value equivalence meant here, not object identity),
	 * the method returns a reference to the node. NULL otherwise.
	 **/
	GeneralAVLTreeNode *findNode(void *key);

	/** Returns the node with the biggest value such that node->value <= key. **/
	GeneralAVLTreeNode *findBiggestSmallerEq(void *key);

	/** Returns the node with the smallest value such that node->value >= key. **/
	GeneralAVLTreeNode *findSmallestBiggerEq(void *key);

	/** Returns the left-most node of the tree. NULL if the tree is empty. **/
	GeneralAVLTreeNode *getLeftMost();

	/** Returns the right-most node of the tree. NULL if the tree is empty. **/
	GeneralAVLTreeNode *getRightMost();

	/**
	 * Gets the next node in the tree, i.e. the node whose value is the smallest
	 * among those having bigger value than "currentNode". This method can be used
	 * to walk through the tree.
	 **/
	GeneralAVLTreeNode *getNext(GeneralAVLTreeNode *currentNode);

	/** Returns the number of nodes in the tree. **/
	int32_t getNodeCount();

	/**
	 * Returns the number of the node whose "value" member corresponds to the value
	 * of parameter "key". -1 if there is no such node.
	 **/
	int32_t getNodeNumber(void *key);

	/**
	 * Returns a pointer to a sorted list of the elements (value fields) stored
	 * in the tree. The memory has to be freed by the caller.
	 **/
	void **createSortedList();

	/**
	 * Returns a pointer to a sorted list of the elements (data fields) stored in
	 * the subtree rooted at "node". The memory has to be freed by the caller.
	 **/
	void **createSortedList(int32_t nodeID);

	/**
	 * Inserts a new node into the tree, with node value as given by the
	 * parameter "value". Returns 0 on success, -1 on error.
	 **/
	int32_t insertNode(void *value);

	/**
	 * Removes the node with value equal to "key" from the tree. Returns 0 if
	 * successful, -1 otherwise.
	 **/
	int32_t deleteNode(void *key);

	/**
	 * Removes the node with index "node" from the tree. Returns 0 if successuful,
	 * -1 otherwise.
	 **/
	int32_t deleteNode(int32_t nodeID);

private:

	/** Returns the size of the subtree rooted at node "node". **/
	int32_t getSizeOfSubtree(int32_t nodeID);

	/** Internal helper function for "createSortedList". **/
	void **storeSortedList(int32_t nodeID, void **array);

	/** Returns the height of the node given by "node". **/
	int32_t getNodeHeight(int32_t nodeID);

	void rotate(int32_t parent, int32_t child);

	void rebalanceHereOrAbove(int32_t nodeID);

	int32_t createNode(void *value);

	int32_t getHeight(int32_t nodeID);

}; // end of class GeneralAVLTree


#endif

