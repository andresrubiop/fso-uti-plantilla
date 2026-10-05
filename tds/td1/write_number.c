/*
 * TD1 - exercise 4: write the memory content of a long, then the number of bytes written.
 * The output is binary, not text: look at it with  ./write_number | od -A d -t x1
 */
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

int main(void)
{
    long value = 5;
    ssize_t written;

    written = write(STDOUT_FILENO, &value, sizeof value);    /* 8 bytes on x86-64 */
    exit_if(written == -1, "write value");

    exit_if(write(STDOUT_FILENO, &written, sizeof written) == -1, "write count");
    return EXIT_SUCCESS;
}
