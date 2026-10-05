/*
 * TD2 - exercise 3: dup() creates a second descriptor for the SAME open file description,
 * so both descriptors share the offset. With donnees.txt = "abcdefgh" this prints "abcdefgh".
 */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

#define DATAFILE "donnees.txt"
#define CHUNK 4

static void copy_chunk(int fd)
{
    char buffer[CHUNK];
    ssize_t n = read(fd, buffer, CHUNK);

    exit_if(n == -1, "read");
    exit_if(write_all(STDOUT_FILENO, buffer, (size_t)n) == -1, "write");
}

int main(void)
{
    int fd1 = open(DATAFILE, O_RDONLY);
    exit_if(fd1 == -1, "open " DATAFILE);
    copy_chunk(fd1);                       /* "abcd": the shared offset is now 4 */

    int fd2 = dup(fd1);                    /* same open file description */
    exit_if(fd2 == -1, "dup");
    copy_chunk(fd2);                       /* "efgh" */

    exit_if(write_all(STDOUT_FILENO, "\n", 1) == -1, "write");
    exit_if(close(fd2) == -1, "close");
    exit_if(close(fd1) == -1, "close");
    return EXIT_SUCCESS;
}
