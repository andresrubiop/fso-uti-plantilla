/*
 * TD6 - exercise 1: create 8 threads, wait for them, and compare a global and a local variable.
 * All threads share the global `shared` (same address) but each has its own `local` (on its
 * own stack): the addresses printed show it. `shared++` is a race: see exercise 2.
 * Build: gcc -pthread (the Makefile already does it).
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

#define NTHREADS 8

static int shared = 0;

static void *start(void *arg)
{
    pid_t pid = (pid_t)(intptr_t)arg;       /* the pid travels inside the void * */
    int local = 0;

    shared++;
    local++;
    printf("thread %lu of process %d: shared[%p] = %d, local[%p] = %d\n",
           (unsigned long)pthread_self() % 100000, pid, (void *)&shared, shared, (void *)&local, local);
    return NULL;                            /* same as pthread_exit(NULL) */
}

int main(void)
{
    pthread_t tids[NTHREADS];
    pid_t pid = getpid();

    for (int i = 0; i < NTHREADS; i++) {
        int rc = pthread_create(&tids[i], NULL, start, (void *)(intptr_t)pid);
        exit_if(rc != 0, "pthread_create");  /* pthread functions return the error code */
    }
    for (int i = 0; i < NTHREADS; i++)
        exit_if(pthread_join(tids[i], NULL) != 0, "pthread_join");

    printf("process %d: all %d threads finished, shared = %d\n", pid, NTHREADS, shared);
    return EXIT_SUCCESS;
}
