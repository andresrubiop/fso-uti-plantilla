/*
 * TD5 - exercise 3: protections of an anonymous mapping.
 * PROT is given at compile time; the Makefile builds map_none, map_read, map_write, map_exec
 * and map_ew (PROT_EXEC|PROT_WRITE). Each program reads *p, writes 42 and reads it again.
 * Read mmap(2): on x86-64, PROT_WRITE implies read access; PROT_NONE forbids everything.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "utils.h"

#ifndef PROT
#define PROT (PROT_READ | PROT_WRITE)
#endif

/* Readable name of the protection flags, e.g. "PROT_EXEC|PROT_WRITE". */
static const char *prot_name(int prot)
{
    static char name[64];
    name[0] = '\0';
    if (prot == PROT_NONE)
        return "PROT_NONE";
    if (prot & PROT_READ)  strcat(name, "|PROT_READ");
    if (prot & PROT_WRITE) strcat(name, "|PROT_WRITE");
    if (prot & PROT_EXEC)  strcat(name, "|PROT_EXEC");
    return name + 1;
}

int main(void)
{
    int *p = mmap(NULL, sizeof(int), PROT, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    exit_if(p == MAP_FAILED, "mmap");

    fprintf(stderr, "PROT = %s\n", prot_name(PROT));
    fprintf(stderr, "pointer: %p\n", (void *)p);
    fprintf(stderr, "read:    *p = %d\n", *p);      /* anonymous memory starts zeroed */
    *p = 42;
    fprintf(stderr, "write:   *p = %d\n", *p);

    exit_if(munmap(p, sizeof(int)) == -1, "munmap");
    return EXIT_SUCCESS;
}
