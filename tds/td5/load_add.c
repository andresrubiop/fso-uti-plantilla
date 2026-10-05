/*
 * TD5 - exercise 4: "load" the machine code of add() into a file, like a tiny shared library.
 * Creates libadd.a (2048 bytes), maps it shared and copies the bytes of the function into it.
 * This works because add() is position independent (no absolute address, no call).
 * Real loaders (ld.so, dlopen) also handle relocations, symbols and protections.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "utils.h"

#define LIBFILE "libadd.a"
#define SIZE 2048

int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int fd = open(LIBFILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    exit_if(fd == -1, "open " LIBFILE);
    /* An empty file cannot be mapped usefully (access past its end = SIGBUS): give it its size. */
    exit_if(ftruncate(fd, SIZE) == -1, "ftruncate");

    char *code = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    exit_if(code == MAP_FAILED, "mmap");
    exit_if(close(fd) == -1, "close");

    /* Copy the first bytes of add(). Converting a function pointer to void * is not ISO C,
       but POSIX requires it to work (dlsym relies on it). */
    memcpy(code, (const void *)add, 64);

    exit_if(munmap(code, SIZE) == -1, "munmap");   /* MAP_SHARED: the bytes are in the file */
    return EXIT_SUCCESS;
}
