#include <stdlib.h>
#include <string.h>
#include "dirtree.h"

#define __REENTRANT

/*
 * DirectoryTree data structure
 *  The data structure stores Nodes in a hashtable (implemented as an array of linked lists)
 */

/*
 * Node data structure
 *  A node stores an entry and a pointer to another node.
 *  An entry contains the name of a directory, the length of the name and the index to the node of the directory's parent, (-1 if the Node is the root of the tree)
 */

DirectoryTree::DirectoryTree() {
	data = (Node**) malloc(sizeof(Node*) * HASH);
	memset(data, 0, sizeof(Node*) * HASH);

	cache = (Cache*) malloc(sizeof(Cache) * CACHE_SIZE);
	memset(cache, 0, sizeof(Cache) * CACHE_SIZE);
	for (int i = 0; i < CACHE_SIZE; i++) {
		cache[i].idx = -1;
	}
	clock = 0;
}

DirectoryTree::~DirectoryTree() {
	Node *ptr, *next;
	for (int i = 0; i < HASH; i++) {
		ptr = data[i];
		while (ptr != NULL) {
			if (ptr->entry != NULL) {
				free((void*) ptr->entry->name);
				free((void*) ptr->entry);
			}
			next = ptr->next;
			free((void*) ptr);
			ptr = next;
		}
	}
	free((void*) data);

	for (int i = 0; i < CACHE_SIZE; i++) {
		if (cache[i].idx != -1) {
			free((void*) cache[i].name);
		}
	}
	free((void*) cache);
}

// return full path to the directory
// ** do not free pointer returned **
char* DirectoryTree::get(int idx) {
	// check up in cache
	for (int i = 0; i < CACHE_SIZE; i++) {
		if (cache[i].idx == idx) {
			cache[i].mark = true;
			return cache[i].name;
		}
	}

	int pathLength = recurseLength(idx);
	char *name = NULL;
	if (pathLength != -1) {
		// find spot in cache for new entry using mark and sweep
		while (cache[clock].mark == true) {
			cache[clock].mark = false;
			clock = (clock + 1) % CACHE_SIZE;
		}

		if (cache[clock].idx != -1) {
			free((void*) cache[clock].name);
		}

		// allocate space and clear
		cache[clock].name = (char*) malloc(sizeof(char) * (pathLength + 1));
		memset(cache[clock].name, 0, sizeof(char) * (pathLength + 1));

		// retrieve the name
		if (recurseCopy(idx, cache[clock].name)) {
			cache[clock].idx = idx;
			name = cache[clock].name;
			clock = (clock + 1) % CACHE_SIZE;
		} else {
			cache[clock].idx = -1;
			free((void*) cache[clock].name);
			cache[clock].name = NULL;
		}
	}

	return name;
}

// set the directory entry at idx with Entry containing parent and *name
int DirectoryTree::set(int idx, int parent, char *name) {
	Node *node = data[idx % HASH];
	Node *last = NULL;
	Entry *entry = NULL;
	
	while (node != NULL && node->idx != idx) {
		last = node;
		node = node->next;
	}

	if (node != NULL) {
		// node->idx == idx
		// replace entry with new information
		entry = node->entry;
		free ((void*) entry->name);
		// invalidate the cache
		invalidateCache(idx);
	} else {
		node = (Node*) malloc(sizeof(Node));
		entry = (Entry*) malloc(sizeof(Entry));
		node->entry = entry;
		node->next = NULL;

		if (last == NULL) {
			data[idx % HASH] = node;
		} else {
			last->next = node;
		}
	}

	node->idx = idx;
	entry->parent = parent;
	entry->length = strlen(name);
	entry->name = (char*) malloc (sizeof(char) * (entry->length + 1));
	strcpy(entry->name, name);
}

// remove directory entry at idx
int DirectoryTree::remove(int idx) {
	Node *node = data[idx % HASH];
	Node *last = NULL;

	while (node != NULL && node->idx != idx) {
		last = node;
		node = node->next;
	}

	if (node != NULL) {
		if (last == NULL) {
			data[idx % HASH] = node->next;
		} else {
			last->next = node->next;
		}

		free((void*) node->entry->name);
		free((void*) node->entry);
		free((void*) node);
	}
}

// copy pathname of idx into *ptr recursively
bool DirectoryTree::recurseCopy(int idx, char* ptr) {
	if (idx == -1) {
		return true;
	} else {
		Entry *entry = getEntry(idx);
		if (entry == NULL) {
			return false;
		} else {
			bool recurse = recurseCopy(entry->parent, ptr);
			if (!recurse) {
				return false;
			} else {
				if (entry->parent != -1) {
					strcat(ptr, "/");
				}
				strcat(ptr, entry->name);
				return true;
			}
		}
	}
}

// determine length of pathname
int DirectoryTree::recurseLength(int idx) {
	if (idx == -1) {
		return 0;
	} else {
		Entry *entry = getEntry(idx);
		if (entry == NULL) {
			return -1;
		} else {
			int recurse = recurseLength(entry->parent);
			if (recurse == -1) {
				return -1;
			} else {
				if (entry->parent != -1) {
					return recurse + entry->length + 1;
				} else {
					return recurse + entry->length;
				}
			}
		}
	}
}

// return Entry at idx
Entry* DirectoryTree::getEntry(int idx) {
	Node *node = data[idx % HASH];

	while (node != NULL && node->idx != idx) {
		node = node->next;
	}

	if (node == NULL) {
		return NULL;
	} else {
		return node->entry;
	}
}

// invalidate cached pathname of idx 
void DirectoryTree::invalidateCache(int idx) {
	// invalidate in cache
	for (int i = 0; i < CACHE_SIZE; i++) {
		if (cache[i].idx == idx) {
			cache[i].idx = -1;
			cache[i].mark = false;
			free((void*) cache[i].name);
		}
	}
}

