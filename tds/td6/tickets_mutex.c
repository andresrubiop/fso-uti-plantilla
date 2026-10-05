/*
 * TD6 - exercise 2, solution 1: a mutex protects the check AND the update.
 * The test must be repeated inside the lock: the value read before locking may be stale.
 */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

#define SELLERS 8

static int tickets = 1000;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void *seller(void *arg)
{
    int id = (int)(intptr_t)arg;
    long sold = 0;

    for (;;) {
        pthread_mutex_lock(&lock);
        if (tickets <= 0) {                    /* checked with the lock held */
            pthread_mutex_unlock(&lock);
            break;
        }
        sold++;
        sched_yield();
        tickets--;
        pthread_mutex_unlock(&lock);
    }
    printf("[%d] I sold %ld tickets\n", id, sold);
    return (void *)(intptr_t)sold;
}

int main(int argc, char *argv[])
{
    pthread_t tids[SELLERS];
    int initial = argc > 1 ? atoi(argv[1]) : 1000;
    long total = 0;

    tickets = initial;
    for (int i = 0; i < SELLERS; i++)
        exit_if(pthread_create(&tids[i], NULL, seller, (void *)(intptr_t)i) != 0, "pthread_create");
    for (int i = 0; i < SELLERS; i++) {
        void *ret;
        exit_if(pthread_join(tids[i], &ret) != 0, "pthread_join");
        total += (long)(intptr_t)ret;
    }
    printf("%ld tickets sold out of %d (tickets = %d)\n", total, initial, tickets);
    return total == initial ? EXIT_SUCCESS : EXIT_FAILURE;
}
