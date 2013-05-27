#define CACHE_SIZE (64)
#define HASH (32768)

struct Entry {
	int length;
	int parent;
	char *name;
};

struct Node {
	int idx;
	Node *next;
	Entry *entry;
};

struct Cache {
	int idx;
	bool mark;
	char *name;
};

// do not free pointers returned by DirectoryTree functions
class DirectoryTree {
	public:
		DirectoryTree();
		virtual ~DirectoryTree();

		char* get(int idx);
		int set(int idx, int parent, char *name);
		int remove(int idx);
		Entry* getEntry(int idx);
		void invalidateCache(int idx);
	private:
		Node **data;
		Cache *cache;
		int clock;

		bool recurseCopy(int idx, char* ptr);
		int recurseLength(int idx);
};
