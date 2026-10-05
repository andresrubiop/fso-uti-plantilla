/*
 * TD5 - exercise 2: sender. Reads the receiver's pid at the start of block.txt, copies its
 * standard input after it (at most SIZE - sizeof(pid) - 1 bytes) and sends SIGUSR1.
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

int main(void)
{
    int fd = open(FILENAME, O_RDWR);
    exit_if(fd == -1, "open " FILENAME);
    char *shared = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    exit_if(shared == MAP_FAILED, "mmap");
    exit_if(close(fd) == -1, "close");

    pid_t pid;
    memcpy(&pid, shared, sizeof pid);
    printf("sender: the receiver is %d\n", pid);

    /* Copy stdin into the message area, keeping one byte for '\0'. */
    size_t off = sizeof pid, max = SIZE - 1;
    ssize_t n = 1;
    while (off < max && (n = read(STDIN_FILENO, shared + off, max - off)) > 0)
        off += (size_t)n;
    exit_if(n == -1, "read");
    shared[off] = '\0';

    exit_if(msync(shared, SIZE, MS_SYNC) == -1, "msync");   /* optional: also push to the file */
    exit_if(kill(pid, SIGUSR1) == -1, "kill");
    exit_if(munmap(shared, SIZE) == -1, "munmap");
    return EXIT_SUCCESS;
}
