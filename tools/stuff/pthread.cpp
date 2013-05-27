#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>


static int fd;


static void *function(void *data) {
	char line[1024];
	usleep(500000);
	printf("Opening file.\n");
	fd = open("/tmp/fifo", O_RDONLY);
	if (fd < 0)
		printf("Unable to open file.\n");
	else {
		printf("File opened.\n");
	}
	printf("Thread finished.\n");
	return NULL;
}


int main() {
	pthread_t thread;
	pthread_create(&thread, NULL, function, NULL);
	pthread_detach(thread);
	usleep(2000000);
	printf("Closing file.\n");
	close(fd);
	usleep(10000000);
	return 0;
}


