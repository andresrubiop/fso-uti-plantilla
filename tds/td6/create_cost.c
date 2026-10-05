/*
 * TD6 - exploration 1: how much does creating a thread cost compared with a process? (original slide 136)
 * Creates and waits for N threads (pthread_create + pthread_join) and N processes (fork + waitpid, the
 * child exits at once) and prints the average time of each.
 *   ./create_cost [N]      default: 2000
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

static double now(void)
{
    struct timespec ts;
    exit_if(clock_gettime(CLOCK_MONOTONIC, &ts) == -1, "clock_gettime");
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *nothing(void *arg)
{
    return arg;
}

int main(int argc, char *argv[])
{
    int n = argc > 1 ? atoi(argv[1]) : 2000;

    double t0 = now();
    for (int i = 0; i < n; i++) {
        pthread_t t;
        int rc = pthread_create(&t, NULL, nothing, NULL);
        exit_if(rc != 0, "pthread_create");
        exit_if(pthread_join(t, NULL) != 0, "pthread_join");
    }
    double thread_us = (now() - t0) * 1e6 / n;

    t0 = now();
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        exit_if(pid == -1, "fork");
        if (pid == 0)
            _exit(0);
        exit_if(waitpid(pid, NULL, 0) == -1, "waitpid");
    }
    double fork_us = (now() - t0) * 1e6 / n;

    printf("%d threads:   %6.1f us per pthread_create + pthread_join\n", n, thread_us);
    printf("%d processes: %6.1f us per fork + waitpid\n", n, fork_us);
    printf("a process costs %.1f times a thread\n", fork_us / thread_us);
    return EXIT_SUCCESS;
}
