/*
 * TD6 - exercise 2: 8 sellers (threads) share the global counter `tickets`. NO protection.
 *   ./tickets_race [tickets]      (default 1000)
 * The check (tickets > 0) and the update (tickets--) are separate steps and tickets-- itself is
 * load / subtract / store: between them another thread can run (sched_yield makes it likely).
 * Result: more tickets sold than available, and a counter that ends below zero.
 */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

#define SELLERS 8

static volatile int tickets = 1000;    /* volatile only prevents caching in a register:
                                          it does NOT make the accesses atomic */

static void *seller(void *arg)
{
    int id = (int)(intptr_t)arg;
    long sold = 0;

    while (tickets > 0) {              /* critical section starts here ...          */
        sold++;
        sched_yield();                 /* do not touch this line                     */
        tickets--;                     /* ... and ends here                          */
    }
    printf("[%d] I sold %ld tickets\n", id, sold);
    return (void *)(intptr_t)sold;     /* same as pthread_exit((void *)sold) */
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
