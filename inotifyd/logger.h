#ifndef __LOGGER_H
#define __LOGGER_H

#define BUFFER_SIZE 65536

void *startLogger(void* logger);

class Logger {
	public:
		pthread_t thread;
		pthread_t dummyThread;
		Logger(char* outputFileName);
		virtual ~Logger();
		char* outputFN;

		void enqueue(char* ptr);
		void start(bool dummy);
		void run();
		void dummyThreadRun();
	private:
		pthread_mutex_t lock;
		pthread_cond_t empty;
		int head;
		int tail;
		char **buffer;
		int used[BUFFER_SIZE];
		FILE* outputFD;
};

#endif

