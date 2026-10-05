/*
 * TD6 - exploration 2: Peterson's algorithm on a real multicore x86 (original slides 152 and 154).
 * Two threads increment a shared counter N times each, protecting the increment with Peterson's lock.
 * The algorithm is correct on paper, but it "assumes memory accesses are sequentially ordered per thread"
 * (slide 154). Without a fence, the x86 store buffer lets each thread read the other's flag BEFORE its own
 * store is visible: both enter the critical section and increments are lost. With a full fence
 * (mfence, via __atomic_thread_fence) the algorithm works. Compiled twice: peterson (no fence) and
 * peterson_fence (-DFENCE).
 *   ./peterson [N]      default: 2000000
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

static volatile int flag[2];
static volatile int turn;
static volatile long counter;
static long n;

static void *worker(void *arg)
{
    int me = (int)(long)arg, other = 1 - me;
    for (long i = 0; i < n; i++) {
        flag[me] = 1;
        turn = other;
#ifdef FENCE
        __atomic_thread_fence(__ATOMIC_SEQ_CST);      /* the stores must be visible before the loads */
#endif
        while (flag[other] && turn == other)
            ;                                         /* busy waiting */
        counter = counter + 1;                        /* critical section */
        flag[me] = 0;
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    n = argc > 1 ? atol(argv[1]) : 2000000;
    pthread_t t[2];
    for (long i = 0; i < 2; i++)
        exit_if(pthread_create(&t[i], NULL, worker, (void *)i) != 0, "pthread_create");
    for (int i = 0; i < 2; i++)
        exit_if(pthread_join(t[i], NULL) != 0, "pthread_join");
#ifdef FENCE
    const char *variant = "with fence";
#else
    const char *variant = "no fence";
#endif
    printf("Peterson %-10s: counter = %ld, expected %ld, lost %ld\n", variant, counter, 2 * n, 2 * n - counter);
    return EXIT_SUCCESS;
}
