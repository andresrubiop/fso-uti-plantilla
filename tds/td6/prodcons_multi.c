/*
 * TD6 - exercise 3 (b): one producer, N consumers, and a bounded buffer (the classic of the
 * lecture). The producer puts ITEMS values in a circular buffer of BUF cells; the consumers
 * take them out and add them to a shared sum. Every value must be consumed exactly once.
 *
 *   empty  = number of free cells  (starts at BUF)  - the producer waits on it
 *   full   = number of used cells  (starts at 0)    - the consumers wait on it
 *   mutex  = protects the `out` index shared by the consumers (new critical section!)
 *   sum_lock protects the shared sum
 * To stop, the producer sends one "poison pill" (value -1) per consumer.
 *   ./prodcons_multi [consumers] [items]      (default 4 consumers, 100000 items)
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

#define BUF 16
#define POISON (-1)

static int buffer[BUF];
static int in = 0, out = 0;                 /* in: producer only; out: shared by consumers */
static sem_t empty, full;
static pthread_mutex_t out_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sum_lock = PTHREAD_MUTEX_INITIALIZER;
static long long sum = 0;
static int nconsumers = 4, items = 100000;

static void *producer(void *arg)
{
    (void)arg;
    for (int v = 1; v <= items + nconsumers; v++) {
        int value = v <= items ? v : POISON;
        sem_wait(&empty);                   /* wait for a free cell */
        buffer[in] = value;                 /* only one producer: no lock needed for `in` */
        in = (in + 1) % BUF;
        sem_post(&full);                    /* one more item available */
    }
    return NULL;
}

static void *consumer(void *arg)
{
    long mine = 0, count = 0;
    for (;;) {
        sem_wait(&full);
        pthread_mutex_lock(&out_lock);      /* two consumers must not take the same cell */
        int value = buffer[out];
        out = (out + 1) % BUF;
        pthread_mutex_unlock(&out_lock);
        sem_post(&empty);
        if (value == POISON)
            break;
        mine += value;
        count++;
    }
    pthread_mutex_lock(&sum_lock);          /* add the partial sum once, not per item */
    sum += mine;
    pthread_mutex_unlock(&sum_lock);
    printf("consumer %d: %ld items\n", (int)(intptr_t)arg, count);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc > 1) nconsumers = atoi(argv[1]);
    if (argc > 2) items = atoi(argv[2]);
    pthread_t prod, cons[nconsumers];

    exit_if(sem_init(&empty, 0, BUF) == -1 || sem_init(&full, 0, 0) == -1, "sem_init");
    exit_if(pthread_create(&prod, NULL, producer, NULL) != 0, "pthread_create");
    for (int i = 0; i < nconsumers; i++)
        exit_if(pthread_create(&cons[i], NULL, consumer, (void *)(intptr_t)i) != 0, "pthread_create");
    exit_if(pthread_join(prod, NULL) != 0, "pthread_join");
    for (int i = 0; i < nconsumers; i++)
        exit_if(pthread_join(cons[i], NULL) != 0, "pthread_join");

    long long expected = (long long)items * (items + 1) / 2;
    printf("sum = %lld, expected %lld: %s\n", sum, expected, sum == expected ? "OK" : "WRONG");
    return sum == expected ? EXIT_SUCCESS : EXIT_FAILURE;
}
