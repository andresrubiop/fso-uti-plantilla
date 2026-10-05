/*
 * TD4 - exercise 3: ps | uppercase, built by hand.
 * The child reads the pipe until end of file and prints it in uppercase.
 * The parent redirects its stdout to the pipe (dup2) and becomes `ps` (execlp).
 * The child sees end of file when `ps` exits, because it was the last writer.
 */
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

#define BUFFER_SIZE 512

static void to_upper(int fd_in, int fd_out)
{
    char buffer[BUFFER_SIZE];
    ssize_t n;

    while ((n = read(fd_in, buffer, sizeof buffer)) > 0) {     /* until end of file */
        for (ssize_t i = 0; i < n; i++)
            buffer[i] = (char)toupper((unsigned char)buffer[i]);
        exit_if(write_all(fd_out, buffer, (size_t)n) == -1, "write");
    }
    exit_if(n == -1, "read");
}

int main(void)
{
    int fds[2];

    exit_if(pipe(fds) == -1, "pipe");
    pid_t pid = fork();
    exit_if(pid == -1, "fork");

    if (pid == 0) {                               /* child */
        exit_if(close(fds[1]) == -1, "close");    /* otherwise it never sees end of file */
        to_upper(fds[0], STDOUT_FILENO);
        return EXIT_SUCCESS;
    }

    /* parent */
    exit_if(close(fds[0]) == -1, "close");
    exit_if(dup2(fds[1], STDOUT_FILENO) == -1, "dup2");
    exit_if(close(fds[1]) == -1, "close");        /* 1 already refers to the pipe */
    execlp("ps", "ps", (char *)NULL);             /* "ps" twice: file to run, then argv[0] */
    exit_if(1, "execlp ps");
    return EXIT_FAILURE;
}
