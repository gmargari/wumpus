/**
 * Creates a large file at the given destination and does some random I/O to
 * measure the disk seek latency.
 *
 * Usage:  measure_disk_seeks FILENAME
 *
 * author: Stefan Buettcher
 * created: 2006-12-08
 * changed: 2007-03-17
 **/

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>


using namespace std;


static const int BUFFER_SIZE = 4096;
static char buffer[BUFFER_SIZE + 4096];


double getCurrentTime() {
	struct timeval currentTime;
	int result = gettimeofday(&currentTime, NULL);
	assert(result == 0);
	return currentTime.tv_sec + 1E-6 * currentTime.tv_usec;
}


void readFromFile(int fd, long long pos, char *b) {
	if (pos & 4095)
		pos = (pos | 4095) + 1;
	lseek(fd, pos, SEEK_SET);
	read(fd, b, BUFFER_SIZE);
}


int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage:  measure_disk_seeks FILENAME\n\n");
		fprintf(stderr, "Assumes that a sufficiently large file FILENAME exists (raw partition?).\n");
		fprintf(stderr, "Measures the hard drive's average disk seek latency.\n\n");
		return 1;
	}

	double results[1000];
	int cnt = 0;

	char *b = (char*)((((long)buffer) | 4095) + 1);
	int fd = open(argv[1], O_RDONLY | O_DIRECT);
	assert(fd >= 0);
	long long fileSize = lseek(fd, 0, SEEK_END);
	printf("File size: %lld bytes.\n", fileSize);
	static const int STEP_SIZE = 16384;
	static const int ITERATIONS = 1234;

	for (long long distance = STEP_SIZE; distance < 1E11; distance += distance) {
		double totalTime = 0;
		for (int i = 0; i < ITERATIONS; i++) {
			long long pos = 0;
			while ((pos <= distance) || (pos >= fileSize - distance - 4096))
				pos = (random() % (fileSize / 512)) * 512;
			readFromFile(fd, pos, b);
			double startTime = getCurrentTime();
			if (random() % 2)
				readFromFile(fd, pos - distance, b);
			else
				readFromFile(fd, pos + distance, b);
			double endTime = getCurrentTime();
			totalTime += endTime - startTime;
		}
		printf("Distance = %lld. Average time: %.4lf seconds.\n", distance, totalTime / ITERATIONS);
		results[cnt++] = totalTime / ITERATIONS;
	}
	close(fd);

	for (int i = 0; i < cnt; i++)
		printf("Seek distance: STEP_SIZE * 2^%d bytes. Average time per seek: %.4lf seconds.\n", i, results[i]);
	return 0;
} // end of main(int, char**)


