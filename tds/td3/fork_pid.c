/*
 * TD3 - exercise 3: print the pid WITHOUT a newline, fork, and let only the child print.
 *   SOLUTION=0  the bug: "My pid is ..." appears twice (the unflushed stdio buffer is copied by fork)
 *   SOLUTION=1  fflush(stdout) before fork()
 *   SOLUTION=2  no stdio buffer for stdout: setvbuf(stdout, NULL, _IONBF, 0)
 *   SOLUTION=3  the parent leaves with _exit(): its copy of the buffer is never flushed,
 *               the child's copy is printed together with the child's line
 * Make builds the four variants: fork_pid_0 ... fork_pid_3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#ifndef SOLUTION
#define SOLUTION 1
#endif

int main(void)
{
#if SOLUTION == 2
    exit_if(setvbuf(stdout, NULL, _IONBF, 0) != 0, "setvbuf");
#endif
    printf("My pid is %d / ", getpid());     /* no '\n': stays in the buffer */
#if SOLUTION == 1
    fflush(stdout);
#endif

    pid_t pid = fork();
    exit_if(pid == -1, "fork");

    if (pid == 0) {                           /* child */
        printf("child %d, parent %d\n", getpid(), getppid());
        exit(EXIT_SUCCESS);                   /* exit flushes the child's buffer */
    }

    /* Parent: prints nothing after fork, but must wait for its child. */
    int status;
    exit_if(waitpid(pid, &status, 0) == -1, "waitpid");
#if SOLUTION == 3
    _exit(EXIT_SUCCESS);                      /* do not flush the parent's copy */
#else
    return EXIT_SUCCESS;                      /* returning from main = exit(): flushes */
#endif
}
