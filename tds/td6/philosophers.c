/*
 * TD6 - exercise 4: the dining philosophers. N philosophers, N chopsticks (one mutex each).
 * Every philosopher takes the RIGHT chopstick, then the LEFT one.
 *   DEMO_DEADLOCK (Makefile target philosophers_deadlock): everybody may hold the right
 *   chopstick and wait forever for the left one: circular wait = deadlock. A pause between
 *   the two lock operations makes it happen almost every time.
 *   default (solution): break the circular wait by ordering the resources - every
 *   philosopher takes the chopstick with the SMALLER number first. The last philosopher
 *   (whose right chopstick is N-1 and left is 0) therefore starts with the left one.
 *   ./philosophers [meals]   (default 5 meals each)
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

#define N 8

static pthread_mutex_t chopstick[N];
static int meals = 5;

static void think(int id)
{
    printf("%2d: thinking\n", id);
    usleep((useconds_t)(random() % 2000));
    printf("%2d: hungry\n", id);
}

static void eat(int id)
{
    printf("%2d: eating\n", id);
    usleep((useconds_t)(random() % 2000));
    printf("%2d: full\n", id);
}

static void *philosopher(void *arg)
{
    int id = (int)(intptr_t)arg;
    int right = id, left = (id + 1) % N;
#ifdef DEMO_DEADLOCK
    int first = right, second = left;
#else
    int first = right < left ? right : left;       /* global order on the chopsticks */
    int second = right < left ? left : right;
#endif
    for (int m = 0; m < meals; m++) {
        think(id);
        pthread_mutex_lock(&chopstick[first]);
#ifdef DEMO_DEADLOCK
        usleep(20000);                              /* everybody grabs the first one... */
#endif
        pthread_mutex_lock(&chopstick[second]);
        eat(id);
        pthread_mutex_unlock(&chopstick[second]);
        pthread_mutex_unlock(&chopstick[first]);
    }
    printf("%2d: done after %d meals\n", id, meals);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t tids[N];

    if (argc > 1)
        meals = atoi(argv[1]);
    setvbuf(stdout, NULL, _IOLBF, 0);      /* line by line, even into a pipe: see who is stuck */
    srandom(2026);
    for (int i = 0; i < N; i++)
        exit_if(pthread_mutex_init(&chopstick[i], NULL) != 0, "pthread_mutex_init");
    for (int i = 0; i < N; i++)
        exit_if(pthread_create(&tids[i], NULL, philosopher, (void *)(intptr_t)i) != 0, "pthread_create");
    for (int i = 0; i < N; i++)
        exit_if(pthread_join(tids[i], NULL) != 0, "pthread_join");
    printf("dinner finished: %d philosophers x %d meals\n", N, meals);
    return EXIT_SUCCESS;
}
