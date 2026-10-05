/*
 * TD6 - exercise 2, solution 2: the semaphore IS the counter of available tickets.
 * sem_trywait decrements it atomically if it is > 0, or fails (EAGAIN) when it reaches 0:
 * "check and take" become a single indivisible operation, no shared int is needed.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

#define SELLERS 8

static sem_t available;

static void *seller(void *arg)
{
    int id = (int)(intptr_t)arg;
    long sold = 0;

    while (sem_trywait(&available) == 0) {     /* took one ticket */
        sold++;
        sched_yield();
    }
    exit_if(errno != EAGAIN, "sem_trywait");   /* EAGAIN = no tickets left, anything else is a bug */
    printf("[%d] I sold %ld tickets\n", id, sold);
    return (void *)(intptr_t)sold;
}

int main(int argc, char *argv[])
{
    pthread_t tids[SELLERS];
    int initial = argc > 1 ? atoi(argv[1]) : 1000;
    long total = 0;
    int left;

    /* pshared = 0: shared between the threads of this process only */
    exit_if(sem_init(&available, 0, (unsigned)initial) == -1, "sem_init");
    for (int i = 0; i < SELLERS; i++)
        exit_if(pthread_create(&tids[i], NULL, seller, (void *)(intptr_t)i) != 0, "pthread_create");
    for (int i = 0; i < SELLERS; i++) {
        void *ret;
        exit_if(pthread_join(tids[i], &ret) != 0, "pthread_join");
        total += (long)(intptr_t)ret;
    }
    exit_if(sem_getvalue(&available, &left) == -1, "sem_getvalue");
    printf("%ld tickets sold out of %d (tickets = %d)\n", total, initial, left);
    exit_if(sem_destroy(&available) == -1, "sem_destroy");
    return total == initial ? EXIT_SUCCESS : EXIT_FAILURE;
}
