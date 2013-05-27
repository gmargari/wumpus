#ifndef __INOTIFYD_H_
#define __INOTIFYD_H_

struct dirmove_t {
	__u32 cookie;
	inotify_event *from;
	inotify_event *to;
	char *fromName;
	char *toName;
	dirmove_t* next;
};

struct Event {
	Event *next;
	__u32 cookie;
	inotify_event *from;
	inotify_event *to;
};

class DirectoryTree;

class MountPoint {
	public:
		bool valid;
		char *mountPointName;
		char *deviceName;
		int pid;
		pthread_t watchThread;
		pthread_t scanThread;

		MountPoint(char* device, char* mount);
		virtual ~MountPoint();
		void start();

		void watchFileDescriptor();
		void scanMount();
	private:
		int st_dev;
		int fd;
		int maxWD;

		DirectoryTree *tree;

		int directoryEvent(inotify_event *event);
		void scanDirectory(char *path, char *name, int parent, int depth);
		char* getFileName(inotify_event *event);
};

class MtabWatch {
	public:
		pthread_t thread;
		pthread_mutex_t lock;
		pthread_cond_t modified;

		MtabWatch();
		virtual ~MtabWatch();
		void waitForChange();
		void start();
		void run();
};

char** getmtab(int &count);
void *startScan(void* mount);
void *startThread(void* mount);
void init(char* outputFilename, int watchStart, int watchEnd, char** argv, bool dummy);
void daemonize();

void storeFromEvent(inotify_event *i_event, Event* &list);
Event* storeToEvent(inotify_event *i_event, Event *list);
void clearEvent(Event* &list);
Event* getEvent(int wd, char *name, Event *list);

char *timestamp();

#endif
