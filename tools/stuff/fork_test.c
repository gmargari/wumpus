/*
 * fork_test.c - a simple "benchmark" for fork/pthread_create
 *
 * to compile on Linux (2.6 kernels):
 * gcc -Wall -Werror -D_REENTRANT -Os -static -o fork_test fork_test.c -lrt -lpthread
 * 
 * to compile on FreeBSD (5.x):
 * gcc -Wall -Werror -D_REENTRANT -Os -static -o fork_test fork_test.c \
 *     -lpthread
 *
 * to compile on FreeBSD (4.x, you will probably have to use CLOCK_REALTIME):
 * gcc -Wall -Werror -D_REENTRANT -Os -static -o fork_test fork_test.c -pthread
 *
 * don't forget to strip binary afterwards, using:
 * strip -R .note -R .comment fork_test
 */
#define _GNU_SOURCE 1
#define _BSD_SOURCE 1
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#ifdef __GLIBC_PREREQ
# if __GLIBC_PREREQ(2,3)
#  define HAVE_SCHED_SETAFFINITY 1
# else
#  undef HAVE_SCHED_SETAFFINITY
# endif /* __GLIBC_PREREQ */
#endif /* __GLIBC_PREREQ */

#define NUM_RUNS 100000
#define CLOCK_ID CLOCK_MONOTONIC

void *
thread_func (void * foo)
{
  return (NULL);
}

int
main ()
{
  pid_t pid;
  int i;
  pthread_t thread;
  unsigned long long int acc = 0, oacc = 0, res = 0;
  struct timespec start, end;

  printf ("Measuring fork, vfork and pthread_create performance\n" \
	  "with %u runs each.\n\n", NUM_RUNS);
  fflush (stdout);

  for (i=0; i < NUM_RUNS; i++) {
    memset (&start, 0, sizeof (start));
    memset (&end, 0, sizeof (end));
    clock_gettime (CLOCK_ID, &start);
    if ((pid = fork ())) {
      clock_gettime (CLOCK_ID, &end);

      if (-1 == pid) {
	fprintf (stderr, "fork, run: %i\n", i);
	exit (1);
      }
      while ((-1 != wait (NULL)) && (errno != ECHILD));
      if (0 == end.tv_sec && 0 == end.tv_nsec) {
	fprintf (stderr, "clock_gettime\n");
	exit (1);
      }
      if (0 == start.tv_sec && 0 == start.tv_nsec) {
	fprintf (stderr, "clock_gettime\n");
	exit (1);
      }
      if (start.tv_sec > end.tv_sec) {
	fprintf (stderr, "time not strictly monotonic increasing\n");
	exit (1);
      }
      if (start.tv_sec == end.tv_sec && start.tv_nsec > end.tv_nsec) {
	fprintf (stderr, "time not strictly monotonic increasing\n");
	exit (1);
      }

      end.tv_sec = end.tv_sec - start.tv_sec;
      end.tv_nsec = end.tv_nsec - start.tv_nsec;

      oacc = acc;
      acc += ((unsigned long long int) end.tv_sec) * \
	((unsigned long long int) 1000000000);
      acc += ((unsigned long long int) end.tv_nsec);
      if (oacc > acc) {
	fprintf (stderr, "accumulator overflow, try using less runs\n");
	exit (1);
      }
    } else {
      _exit (0);
    }
  }

  res = acc / NUM_RUNS;
  acc = oacc = 0;

  printf ("measured fork time: %llu nsecs\n", res);

  for (i=0; i < NUM_RUNS; i++) {
    memset (&start, 0, sizeof (start));
    memset (&end, 0, sizeof (end));
    clock_gettime (CLOCK_ID, &start);
    if ((pid = vfork ())) {
      clock_gettime (CLOCK_ID, &end);

      if (-1 == pid) {
	fprintf (stderr, "vfork, run: %i\n", i);
	exit (1);
      }
      while ((-1 != wait (NULL)) && (errno != ECHILD));
      if (0 == end.tv_sec && 0 == end.tv_nsec) {
	fprintf (stderr, "clock_gettime\n");
	exit (1);
      }
      if (0 == start.tv_sec && 0 == start.tv_nsec) {
	fprintf (stderr, "clock_gettime\n");
	exit (1);
      }
      if (start.tv_sec > end.tv_sec) {
	fprintf (stderr, "time not strictly monotonic increasing\n");
	exit (1);
      }
      if (start.tv_sec == end.tv_sec && start.tv_nsec > end.tv_nsec) {
	fprintf (stderr, "time not strictly monotonic increasing\n");
	exit (1);
      }

      end.tv_sec = end.tv_sec - start.tv_sec;
      end.tv_nsec = end.tv_nsec - start.tv_nsec;

      oacc = acc;
      acc += ((unsigned long long int) end.tv_sec) * \
	((unsigned long long int) 1000000000);
      acc += ((unsigned long long int) end.tv_nsec);
      if (oacc > acc) {
	fprintf (stderr, "accumulator overflow, try using less runs\n");
	exit (1);
      }
    } else {
      _exit (0);
    }
  }

  res = acc / NUM_RUNS;
  acc = oacc = 0;

  printf ("measured vfork time: %llu nsecs\n", res);

  for (i=0; i < NUM_RUNS; i++) {
    memset (&start, 0, sizeof (start));
    memset (&end, 0, sizeof (end));
    clock_gettime (CLOCK_ID, &start);
    if (!(pthread_create (&thread, NULL, thread_func, NULL))) {
      clock_gettime (CLOCK_ID, &end);
      pthread_join (thread, NULL);

      if (0 == end.tv_sec && 0 == end.tv_nsec) {
	fprintf (stderr, "clock_gettime\n");
	exit (1);
      }
      if (0 == start.tv_sec && 0 == start.tv_nsec) {
	fprintf (stderr, "clock_gettime\n");
	exit (1);
      }
      if (start.tv_sec > end.tv_sec) {
	fprintf (stderr, "time not strictly monotonic increasing\n");
	exit (1);
      }
      if (start.tv_sec == end.tv_sec && start.tv_nsec > end.tv_nsec) {
	fprintf (stderr, "time not strictly monotonic increasing\n");
	exit (1);
      }

      end.tv_sec = end.tv_sec - start.tv_sec;
      end.tv_nsec = end.tv_nsec - start.tv_nsec;

      oacc = acc;
      acc += ((unsigned long long int) end.tv_sec) * \
	((unsigned long long int) 1000000000);
      acc += ((unsigned long long int) end.tv_nsec);
      if (oacc > acc) {
	fprintf (stderr, "accumulator overflow, try using less runs\n");
	exit (1);
      }
    } else {
      fprintf (stderr, "pthread_create, run %u\n", i);
      exit (1);
    }
  }

  res = acc / NUM_RUNS;

  printf ("measured pthread_create time: %llu nsecs\n", res);

  exit (0);
}


