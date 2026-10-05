/*
 * TD5 - exercise 1: catch SIGUSR1 with sigaction(2).
 *   ./mysignal [n]      handles SIGUSR1 n times (default 3), then restores the default action
 * From another terminal:  kill -USR1 <pid>,  kill -STOP <pid>,  kill -CONT <pid>,  kill -KILL <pid>
 * After the n-th SIGUSR1 the default action is back: the next SIGUSR1 terminates the process.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

static volatile sig_atomic_t received = 0;

static void on_usr1(int sig)
{
    /* Only async-signal-safe functions here: no printf (it may be interrupted mid-buffer). */
    static const char msg[] = "handler: received signal ";
    char digits[4] = { (char)('0' + sig / 10), (char)('0' + sig % 10), '\n', 0 };
    if (write_all(STDERR_FILENO, msg, sizeof msg - 1) == -1 || write_all(STDERR_FILENO, digits, 3) == -1)
        return;
    received++;
}

int main(int argc, char *argv[])
{
    int max = argc > 1 ? atoi(argv[1]) : 3;
    struct sigaction sa = { 0 }, old;
    sigset_t block, wait_mask;

    printf("pid %d: waiting for SIGUSR1 (%d times)\n", getpid(), max);
    fflush(stdout);

    sa.sa_handler = on_usr1;
    sigemptyset(&sa.sa_mask);           /* nothing extra blocked while the handler runs */
    sa.sa_flags = 0;
    exit_if(sigaction(SIGUSR1, &sa, &old) == -1, "sigaction");

    /* Block SIGUSR1 outside sigsuspend: a signal arriving between the test of `received`
       and the wait stays pending instead of being lost (the pause() race). */
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    exit_if(sigprocmask(SIG_BLOCK, &block, &wait_mask) == -1, "sigprocmask");
    sigdelset(&wait_mask, SIGUSR1);

    while (received < max)
        sigsuspend(&wait_mask);         /* atomically: unblock SIGUSR1 and sleep */

    exit_if(sigaction(SIGUSR1, &old, NULL) == -1, "sigaction restore");
    printf("default action restored: the next SIGUSR1 terminates me\n");
    fflush(stdout);
    exit_if(sigprocmask(SIG_UNBLOCK, &block, NULL) == -1, "sigprocmask");
    for (;;)
        pause();
}
