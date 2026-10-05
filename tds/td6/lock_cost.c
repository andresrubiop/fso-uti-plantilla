/*
 * TD6 - exploration 3: what a pthread mutex really costs (original slides 160-162).
 * T threads do N lock / increment / unlock each on the same mutex. A Linux mutex is a futex: when it is
 * free, locking is a single atomic instruction in user space (no system call); only when a thread must
 * wait does it call futex() to sleep in the kernel. Run it under "strace -f -c -e trace=futex" with 1 and
 * with 8 threads to count the system calls.
 *   ./lock_cost [threads] [N]      default: 1 thread, 1000000
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "utils.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static long counter, n;

static void *worker(void *arg)
{
    for (long i = 0; i < n; i++) {
        pthread_mutex_lock(&lock);
        counter++;
        pthread_mutex_unlock(&lock);
    }
    return arg;
}

int main(int argc, char *argv[])
{
    int threads = argc > 1 ? atoi(argv[1]) : 1;
    n = argc > 2 ? atol(argv[2]) : 1000000;
    pthread_t t[64];
    exit_if(threads < 1 || threads > 64, "threads must be 1..64");

    struct timespec a, b;
    exit_if(clock_gettime(CLOCK_MONOTONIC, &a) == -1, "clock_gettime");
    for (int i = 0; i < threads; i++)
        exit_if(pthread_create(&t[i], NULL, worker, NULL) != 0, "pthread_create");
    for (int i = 0; i < threads; i++)
        exit_if(pthread_join(t[i], NULL) != 0, "pthread_join");
    exit_if(clock_gettime(CLOCK_MONOTONIC, &b) == -1, "clock_gettime");

    double ns = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (double)(threads * n);
    printf("%d thread(s) x %ld lock/unlock: counter = %ld, %.1f ns per lock/unlock\n", threads, n, counter, ns);
    return EXIT_SUCCESS;
}
