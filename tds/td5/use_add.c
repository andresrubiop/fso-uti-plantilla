/*
 * TD5 - exercise 4: map libadd.a with PROT_EXEC and call the code it contains.
 *   ./load_add && ./use_add       ->   f(42, 12) = 54
 * Several use_add running at the same time share the SAME physical page (page cache).
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "utils.h"

#define LIBFILE "libadd.a"
#define SIZE 2048

int main(void)
{
    int fd = open(LIBFILE, O_RDONLY);
    exit_if(fd == -1, "open " LIBFILE);

    void *code = mmap(NULL, SIZE, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    exit_if(code == MAP_FAILED, "mmap");
    exit_if(close(fd) == -1, "close");

    int (*f)(int, int) = (int (*)(int, int))code;
    printf("f(42, 12) = %d\n", f(42, 12));

    exit_if(munmap(code, SIZE) == -1, "munmap");
    return EXIT_SUCCESS;
}
