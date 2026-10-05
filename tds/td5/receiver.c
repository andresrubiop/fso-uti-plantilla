/*
 * TD5 - exercise 2: receiver. Maps block.txt (shared), writes its pid at the start of the
 * mapping, waits for SIGUSR1 and prints the message that the sender left after the pid.
 *   ./receiver &     then     echo "hello" | ./sender
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "utils.h"

#define FILENAME "block.txt"
#define SIZE 128

static void on_usr1(int sig) { (void)sig; }    /* only interrupts sigsuspend */

int main(void)
{
    struct sigaction sa = { 0 };
    sigset_t usr1, old, wait_mask;

    /* 1. Block SIGUSR1 BEFORE publishing the pid: a sender can only signal us after that,
          and the signal will wait (pending) until sigsuspend. */
    sigemptyset(&usr1);
    sigaddset(&usr1, SIGUSR1);
    exit_if(sigprocmask(SIG_BLOCK, &usr1, &old) == -1, "sigprocmask");
    sa.sa_handler = on_usr1;
    sigemptyset(&sa.sa_mask);
    exit_if(sigaction(SIGUSR1, &sa, NULL) == -1, "sigaction");

    /* 2. The file must be at least SIZE bytes: mapping past the end of a file gives SIGBUS. */
    int fd = open(FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0644);
    exit_if(fd == -1, "open " FILENAME);
    exit_if(ftruncate(fd, SIZE) == -1, "ftruncate");
    char *shared = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    exit_if(shared == MAP_FAILED, "mmap");
    exit_if(close(fd) == -1, "close");               /* the mapping stays valid */

    /* 3. Publish our pid. */
    pid_t me = getpid();
    memcpy(shared, &me, sizeof me);
    printf("receiver %d: waiting for SIGUSR1\n", me);
    fflush(stdout);

    /* 4. Sleep with SIGUSR1 unblocked, atomically. */
    wait_mask = old;
    sigdelset(&wait_mask, SIGUSR1);
    sigsuspend(&wait_mask);
    exit_if(sigprocmask(SIG_SETMASK, &old, NULL) == -1, "sigprocmask");   /* SETMASK, not BLOCK */

    shared[SIZE - 1] = '\0';                          /* never trust the other side */
    printf("received: %s", shared + sizeof me);
    exit_if(munmap(shared, SIZE) == -1, "munmap");
    return EXIT_SUCCESS;
}
