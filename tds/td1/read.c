/*
 * TD1 - exercise 6: copy standard input to standard output with read(2)/write(2).
 * Stops at end of file (Ctrl-D in a terminal). Usages:
 *   ./read            ./read < file            ./read < in > out            ./read < .  (error)
 */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 4096
#endif

int main(void)
{
    char buffer[BUFFER_SIZE];

    for (;;) {
        ssize_t n = read(STDIN_FILENO, buffer, BUFFER_SIZE);
        if (n == -1 && errno == EINTR)
            continue;
        exit_if(n == -1, "read");
        if (n == 0)                     /* end of file: the writer closed its side */
            return EXIT_SUCCESS;

        /* write can be partial: loop until the n bytes are out (see write_all in libutils). */
        ssize_t done = 0;
        while (done < n) {
            ssize_t rc = write(STDOUT_FILENO, buffer + done, (size_t)(n - done));
            exit_if(rc == -1, "write");
            done += rc;
        }
    }
}
