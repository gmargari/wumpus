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
 * Implementation of the GeneralAVLTree class.
 *
 * author: Stefan Buettcher
 * created: 2004-10-19
 * changed: 2004-10-20
 **/


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "general_avltree.h"
#include "alloc.h"


GeneralAVLTree::GeneralAVLTree(Comparator *comp) {
	comparator = comp;
	nodes = (GeneralAVLTreeNode*)malloc((INITIAL_NODE_COUNT + 1) * sizeof(GeneralAVLTreeNode));
	nodeCount = 0;
	slotCount = INITIAL_NODE_COUNT;
	for (int i = 0; i < INITIAL_NODE_COUNT; i++)
		nodes[i].value = NULL;
	root = -1;
	freeNodeCount = INITIAL_NODE_COUNT;
	freeNodes = (int32_t*)malloc((INITIAL_NODE_COUNT + 1) * sizeof(int32_t));
	for (int i = 0; i < INITIAL_NODE_COUNT; i++)
		freeNodes[i] = i;
} // end of GeneralAVLTree(Lexicon*)


GeneralAVLTree::~GeneralAVLTree() {
	free(nodes);
	free(freeNodes);
} // end of ~GeneralAVLTree()


int32_t GeneralAVLTree::getNodeCount() {
	return nodeCount;
} // end of getNodeCount()


GeneralAVLTreeNode * GeneralAVLTree::findNode(void *key) {
	assert(key != NULL);
	int32_t nodeID = root;
	while (nodeID >= 0) {
		int comparison = comparator->compare(nodes[nodeID].value, key);
		if (comparison == 0)
			return &nodes[nodeID];
		else if (comparison < 0)
			nodeID = nodes[nodeID].rightChild;
		else
			nodeID = nodes[nodeID].leftChild;
	}
	return NULL;
} // end of findNode(void*)


GeneralAVLTreeNode * GeneralAVLTree::findBiggestSmallerEq(void *key) {
	assert(key != NULL);
	int32_t nodeID = root;
	int32_t candidate = -1;
	while (nodeID >= 0) {
		int comparison = comparator->compare(nodes[nodeID].value, key);
		if (comparison == 0)
			return &nodes[nodeID];
		else if (comparison < 0) {
			candidate = nodeID;
			nodeID = nodes[nodeID].rightChild;
		}
		else
			nodeID = nodes[nodeID].leftChild;
	}
	if (candidate < 0)
		return NULL;
	else
		return &nodes[candidate];
} // end of findBiggestSmallerEq(void*)


GeneralAVLTreeNode * GeneralAVLTree::findSmallestBiggerEq(void *key) {
	assert(key != NULL);
	int32_t nodeID = root;
	int32_t candidate = -1;
	while (nodeID >= 0) {
		int comparison = comparator->compare(nodes[nodeID].value, key);
		if (comparison == 0)
			return &nodes[nodeID];
		else if (comparison < 0)
			nodeID = nodes[nodeID].rightChild;
		else {
			candidate = nodeID;
			nodeID = nodes[nodeID].leftChild;
		}
	}
	if (candidate < 0)
		return NULL;
	else
		return &nodes[candidate];
} // end of findSmallestBiggerEq(void*)


GeneralAVLTreeNode * GeneralAVLTree::getLeftMost() {
	if (root < 0)
		return NULL;
	int32_t nodeID = root;
	while (nodes[nodeID].leftChild >= 0)
		nodeID = nodes[nodeID].leftChild;
	return &nodes[nodeID];
} // end of getLeftMost()


GeneralAVLTreeNode * GeneralAVLTree::getRightMost() {
	if (root < 0)
		return NULL;
	int32_t nodeID = root;
	while (nodes[nodeID].rightChild >= 0)
		nodeID = nodes[nodeID].rightChild;
	return &nodes[nodeID];
} // end of getRightMost()


GeneralAVLTreeNode * GeneralAVLTree::getNext(GeneralAVLTreeNode *currentNode) {
	// if there is a right child, we go there
	if (currentNode->rightChild >= 0)
		return &nodes[currentNode->rightChild];
	int32_t parentID = currentNode->parent;
	if (parentID < 0) {
		// "no parent" means that we are root; if there is no right child, we are done
		return NULL;
	}
	if (comparator->compare(currentNode->value, nodes[parentID].value) < 0) {
		// currentNode is left child of parent => parent is next
		return &nodes[parentID];
	}
	else {
		// currentNode is right child of parent: move up in tree until we find
		// the first node whose value is bigger than that of "currentNode"
		while (parentID >= 0) {
			if (comparator->compare(currentNode->value, nodes[parentID].value) < 0)
				break;
			parentID = nodes[parentID].parent;
		}
		if (parentID < 0) {
			// no bigger node could be found: we are done
			return NULL;
		}
		// move to the left-most node of the subtree rooted at "parentID"
		int nodeID = parentID;
		while (nodes[nodeID].leftChild >= 0)
			nodeID = nodes[nodeID].leftChild;
		return &nodes[nodeID];
	}
} // end of getNext(GeneralAVLTreeNode*)


int32_t GeneralAVLTree::getNodeNumber(void *key) {
	assert(key != NULL);
	int nodeID = root;
	while (nodeID >= 0) {
		int comparison = comparator->compare(nodes[nodeID].value, key);
		if (comparison == 0)
			return nodeID;
		else if (comparison < 0)
			nodeID = nodes[nodeID].rightChild;
		else
			nodeID = nodes[nodeID].leftChild;
	}
	return -1;
} // end of getNodeNumber(void*)


int32_t GeneralAVLTree::getSizeOfSubtree(int32_t nodeID) {
	if (nodeID < 0)
		return 0;
	else
		return 1 + getSizeOfSubtree(nodes[nodeID].leftChild) +
		           getSizeOfSubtree(nodes[nodeID].rightChild);
} // end of getSizeOfSubtree(int32_t)


void ** GeneralAVLTree::storeSortedList(int32_t nodeID, void **array) {
	if (nodeID < 0)
		return array;
	else {
		if (nodes[nodeID].leftChild >= 0)
			array = storeSortedList(nodes[nodeID].leftChild, array);
		array[0] = nodes[nodeID].value;
		array = &array[1];
		if (nodes[nodeID].rightChild >= 0)
			array = storeSortedList(nodes[nodeID].rightChild, array);
		return array;
	}
} // end of storeSortedList(int32_t, void**)


void ** GeneralAVLTree::createSortedList() {
	if (nodeCount <= 0)
		return (void**)malloc(sizeof(void*));
	else {
		void **result = (void**)malloc((nodeCount + 1) * sizeof(void*));
		storeSortedList(root, result);
		return result;
	}
} // end of createSortedList()


void ** GeneralAVLTree::createSortedList(int32_t nodeID) {
	int treeSize = getSizeOfSubtree(nodeID);
	void **result = (void**)malloc((treeSize + 1) * sizeof(void*));
	storeSortedList(nodeID, result);
	return result;
} // end of createSortedList(int32_t)


static inline int32_t max(int32_t a, int32_t b) {
	if (a > b)
		return a;
	else
		return b;
} // end of max(int, int)


void GeneralAVLTree::rotate(int32_t parent, int32_t child) {

	int32_t parentParent = nodes[parent].parent;

	if (child == nodes[parent].leftChild) {
		nodes[parent].leftChild = nodes[child].rightChild;
		if (nodes[child].rightChild >= 0)
			nodes[nodes[child].rightChild].parent = parent;
		nodes[child].parent = parentParent;
		nodes[child].rightChild = parent;
		nodes[parent].parent = child;
		if (parentParent < 0)
			root = child;
		else if (nodes[parentParent].leftChild == parent)
			nodes[parentParent].leftChild = child;
		else
			nodes[parentParent].rightChild = child;
		nodes[parent].height = 1 +
				max(nodes[nodes[parent].leftChild].height,
				    nodes[nodes[parent].rightChild].height);
		nodes[child].height = 1 +
				max(nodes[nodes[child].leftChild].height,
				    nodes[nodes[child].rightChild].height);
		return;
	} // end if (child == nodes[parent].leftChild)
	else if (child == nodes[parent].rightChild) {
		nodes[parent].rightChild = nodes[child].leftChild;
		if (nodes[child].leftChild >= 0)
			nodes[nodes[child].leftChild].parent = parent;
		nodes[child].parent = parentParent;
		nodes[parent].parent = child;
		nodes[child].leftChild = parent;
		if (parentParent < 0)
			root = child;
		else if (nodes[parentParent].leftChild == parent)
			nodes[parentParent].leftChild = child;
		else
			nodes[parentParent].rightChild = child;
		nodes[parent].height = 1 +
				max(nodes[nodes[parent].leftChild].height,
				    nodes[nodes[parent].rightChild].height);
		nodes[child].height = 1 +
				max(nodes[nodes[child].leftChild].height,
				    nodes[nodes[child].rightChild].height);
		return;
	} // end if (child == nodes[parent].rightChild)
	else
		assert("Illegal rotation!" == NULL);

} // end of rotate(int32_t, int32_t)


int32_t GeneralAVLTree::getHeight(int32_t nodeID) {
	assert(nodeID < slotCount);
	assert(nodes[nodeID].value != NULL);
	if (nodeID < 0)
		return 0;
	else
		return nodes[nodeID].height;
} // end of getHeight(int32_t)


void GeneralAVLTree::rebalanceHereOrAbove(int32_t nodeID) {
	while (nodeID >= 0) {
		int32_t leftHeight = getHeight(nodes[nodeID].leftChild);
		int32_t rightHeight = getHeight(nodes[nodeID].rightChild);
		if (leftHeight > rightHeight + 1) {
			// violation type 1 detected
			int32_t leftNode = nodes[nodeID].leftChild;
			if (nodes[nodes[leftNode].leftChild].height > nodes[nodes[leftNode].rightChild].height) {
				// easy case: single rotation
				rotate(nodeID, leftNode);
			}
			else {
				// difficult case: double rotation
				int32_t newLeftNode = nodes[leftNode].rightChild;
				rotate(leftNode, newLeftNode);
				rotate(nodeID, newLeftNode);
			}
			nodes[nodeID].height =
				1 + max(getHeight(nodes[nodeID].leftChild), getHeight(nodes[nodeID].rightChild));
		}
		else if (rightHeight > leftHeight + 1) {
			// violation type 2 detected
			int32_t rightNode = nodes[nodeID].rightChild;
			if (nodes[nodes[rightNode].rightChild].height > nodes[nodes[rightNode].leftChild].height) {
				// easy case: single rotation
				rotate(nodeID, rightNode);
			}
			else {
				// difficult case: double rotation
				int32_t newRightNode = nodes[rightNode].leftChild;
				rotate(rightNode, newRightNode);
				rotate(nodeID, newRightNode);
			}
			nodes[nodeID].height =
				1 + max(getHeight(nodes[nodeID].leftChild), getHeight(nodes[nodeID].rightChild));
		}
		else
			nodes[nodeID].height = 1 + max(leftHeight, rightHeight);
		nodeID = nodes[nodeID].parent;
	}
} // end of rebalanceHereOrAbove(int32_t)


int32_t GeneralAVLTree::createNode(void *value) {
	assert(value != NULL);

	// acquire free node number
	if (freeNodeCount <= 0) {
		// no free node number available: either recompute the list of free node
		// numbers or extend the "nodes" array
		int32_t freeSlotCount = slotCount - nodeCount;
		if (freeSlotCount > nodeCount * (GROWTH_RATE - 1.0)) {
			free(freeNodes);
			freeNodeCount = freeSlotCount;
			freeSlotCount = 0;
			freeNodes = (int32_t*)malloc((freeNodeCount + 1) * sizeof(int32_t));
			for (int i = 0; i < slotCount; i++)
				if (nodes[i].value == NULL)
					freeNodes[freeSlotCount++] = i;
			assert(freeNodeCount == freeSlotCount);
		}
		else {
			// extend "nodes" array
			int32_t newSlotCount = (int32_t)(nodeCount * GROWTH_RATE);
			if (newSlotCount < nodeCount + INITIAL_NODE_COUNT)
				newSlotCount = nodeCount + INITIAL_NODE_COUNT;
			GeneralAVLTreeNode *newNodes =
				(GeneralAVLTreeNode*)malloc((newSlotCount + 1) * sizeof(GeneralAVLTreeNode));
			memcpy(newNodes, nodes, nodeCount * sizeof(GeneralAVLTreeNode));
			free(nodes);
			nodes = newNodes;
			slotCount = newSlotCount;

			// array extended; recompute the list of free node numbers
			freeSlotCount = slotCount - nodeCount;
			free(freeNodes);
			freeNodeCount = freeSlotCount;
			freeSlotCount = 0;
			freeNodes = (int32_t*)malloc((freeNodeCount + 1) * sizeof(int32_t));
			for (int i = 0; i < slotCount; i++)
				if (nodes[i].value == NULL)
					freeNodes[freeSlotCount++] = i;
			assert(freeNodeCount == freeSlotCount);
		}
	} // end if (freeNodeCount <= 0)
	int nodeID = freeNodes[--freeNodeCount];

	nodes[nodeID].value = value;
	return nodeID;
} // end of createNode(void *)


int32_t GeneralAVLTree::insertNode(void *value) {
	int32_t nodeID = createNode(value);

	if (nodeCount == 0) {
		// if we have an empty tree, we make the new node the root
		nodes[nodeID].height = 1;
		nodes[nodeID].leftChild = -1;
		nodes[nodeID].rightChild = -1;
		nodes[nodeID].parent = -1;
		root = nodeID;
		nodeCount++;
		return 0;
	}
	else {
		// non-empty tree: search for the right position
		int comparison;
		int32_t lastNode = -1;
		int32_t currentNode = root;
		while (currentNode >= 0) {
			lastNode = currentNode;
			comparison = comparator->compare(nodes[currentNode].value, value);
			if (comparison == 0) {
				freeNodeCount++;
				return -1;
			}
			else if (comparison < 0)
				currentNode = nodes[currentNode].rightChild;
			else
				currentNode = nodes[currentNode].leftChild;
		}

		// insert the new node at this position in the tree
		currentNode = lastNode;
		if (comparison < 0)
			nodes[currentNode].rightChild = nodeID;
		else
			nodes[currentNode].leftChild = nodeID;

		nodes[nodeID].parent = currentNode;
		nodes[nodeID].leftChild = -1;
		nodes[nodeID].rightChild = -1;
		nodes[nodeID].height = 1;
		rebalanceHereOrAbove(nodeID);

		nodeCount++;
		return 0;
	}

} // end of insertNode(int32_t)


int32_t GeneralAVLTree::deleteNode(void *key) {
	int32_t nodeID = getNodeNumber(key);
	if (nodeID < 0)
		return -1;
	else
		return deleteNode(nodeID);
} // end of deleteNode(void*)


int32_t GeneralAVLTree::deleteNode(int32_t nodeID) {
	int32_t originalNode = nodeID;
	int32_t leftChild = nodes[nodeID].leftChild;
	int32_t rightChild = nodes[nodeID].rightChild;
	int32_t parent = nodes[nodeID].parent;
	
	if (leftChild < 0) {
		// easy case: no left child, simply replace us by the right child
		if (rightChild >= 0)
			nodes[rightChild].parent = parent;
		if (parent < 0)
			root = rightChild;
		else if (nodes[parent].leftChild == nodeID)
			nodes[parent].leftChild = rightChild;
		else
			nodes[parent].rightChild = rightChild;
		nodeID = rightChild;
	}
	else if (rightChild < 0) {
		// easy case: no right child, simply replace us by the left child
		if (leftChild >= 0)
			nodes[leftChild].parent = parent;
		if (parent < 0)
			root = leftChild;
		else if (nodes[parent].leftChild == nodeID)
			nodes[parent].leftChild = leftChild;
		else
			nodes[parent].rightChild = leftChild;
		nodeID = leftChild;
	}
	else {
		// we take the rightmost node on the left-hand side and put it where the old node is
		int32_t candidate = leftChild;
		while (nodes[candidate].rightChild >= 0)
			candidate = nodes[candidate].rightChild;
		if (nodes[candidate].leftChild >= 0)
			nodes[nodes[candidate].leftChild].parent = nodes[candidate].parent;
		if (nodes[nodes[candidate].parent].leftChild == candidate)
			nodes[nodes[candidate].parent].leftChild = nodes[candidate].leftChild;
		else
			nodes[nodes[candidate].parent].rightChild = nodes[candidate].leftChild;
		int32_t whereToCheckForImbalance = nodes[candidate].parent;
		if (whereToCheckForImbalance == nodeID)
			whereToCheckForImbalance = candidate;

		// the candidate has been removed from its old position; put it into its new place
		nodes[candidate].parent = parent;
		if (parent < 0)
			root = candidate;
		else if (nodes[parent].leftChild == nodeID)
			nodes[parent].leftChild = candidate;
		else
			nodes[parent].rightChild = candidate;
		nodes[candidate].leftChild = leftChild;
		nodes[candidate].rightChild = rightChild;
		nodes[leftChild].parent = candidate;
		nodes[rightChild].parent = candidate;
		nodeID = whereToCheckForImbalance;
	}

	freeNodes[freeNodeCount++] = originalNode;
	nodeCount--;
	rebalanceHereOrAbove(nodeID);
	return 0;
} // end of deleteNode(int32_t)


