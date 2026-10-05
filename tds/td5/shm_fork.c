/*
 * TD5 - exercise 5 (end): 16 processes (4 fork() in a loop) register their pid in a shared
 * segment: segment[0] = number of pids, then the pids. The original process prints the list.
 *
 * "count++; table[count] = pid" is NOT atomic: two processes can read the same count and
 * one registration is lost. Build with -DNO_ATOMIC (Makefile target shm_race) to see it;
 * the default version reserves the slot with an atomic fetch-and-add (a preview of unit 7).
 * The segment is IPC_PRIVATE: only this process and its descendants (which inherit the
 * attachment through fork) can see it.
 */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#define FORKS 4                        /* 2^4 = 16 processes */
#define SIZE  4096

int main(int argc, char *argv[])
{
    int forks = argc > 1 ? atoi(argv[1]) : FORKS;
    int id = shmget(IPC_PRIVATE, SIZE, IPC_CREAT | 0600);
    exit_if(id == -1, "shmget");
    int *seg = shmat(id, NULL, 0);
    exit_if(seg == (void *)-1, "shmat");
    /* Mark for deletion now: it disappears when the last process detaches (even on a crash). */
    exit_if(shmctl(id, IPC_RMID, NULL) == -1, "shmctl");

    pid_t original = getpid();
    int children = 0;
    for (int i = 0; i < forks; i++) {
        pid_t pid = fork();
        exit_if(pid == -1, "fork");
        if (pid == 0)
            children = 0;              /* the child starts with no children of its own */
        else
            children++;
    }

#ifdef NO_ATOMIC
    int slot = seg[0] + 1;             /* read ...                 */
    sched_yield();                     /* ... (widen the window) ... */
    seg[0] = slot;                     /* ... write: not atomic    */
#else
    int slot = __atomic_add_fetch(&seg[0], 1, __ATOMIC_SEQ_CST);   /* read-modify-write in one step */
#endif
    if (slot < SIZE / (int)sizeof(int))
        seg[slot] = getpid();

    while (children-- > 0)
        exit_if(wait(NULL) == -1, "wait");

    if (getpid() == original) {
        printf("%d processes registered (expected %d):", seg[0], 1 << forks);
        for (int i = 1; i <= seg[0] && i < SIZE / (int)sizeof(int); i++)
            printf(" %d", seg[i]);
        printf("\n");
    }
    exit_if(shmdt(seg) == -1, "shmdt");
    return EXIT_SUCCESS;
}
