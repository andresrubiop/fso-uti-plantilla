/*
 * TD3 - exploration 1: the memory zones of a process (original slide 62, updated).
 * Prints the address of the code, the data, the heap, a large malloc, a library function and the stack,
 * and the region of /proc/self/maps that contains each one. Run it twice: with ASLR (address space
 * layout randomization) the addresses change on every run, but the order of the zones does not.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

int global_array[10];                   /* uninitialized global: .bss */
static int initialized = 42;            /* initialized global: .data */

/* Finds the line of /proc/self/maps whose range contains addr and prints its permissions and name. */
static void show(const char *what, const void *addr)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    exit_if(maps == NULL, "/proc/self/maps");

    char line[512];
    uintptr_t a = (uintptr_t)addr;
    while (fgets(line, sizeof line, maps) != NULL) {
        unsigned long start, end;
        char perms[5], path[256] = "";
        if (sscanf(line, "%lx-%lx %4s %*s %*s %*s %255[^\n]", &start, &end, perms, path) < 3)
            continue;
        if (a >= start && a < end) {
            char *name = path;
            while (*name == ' ')
                name++;
            printf("%-24s %#14lx   %s  %s\n", what, (unsigned long)a, perms, *name ? name : "(anonymous)");
            break;
        }
    }
    exit_if(fclose(maps) == EOF, "fclose");
}

int main(void)
{
    int local = 0;
    void *small = malloc(10);
    void *big = malloc(64 << 20);        /* above M_MMAP_THRESHOLD (128 KiB): glibc uses mmap */
    exit_if(small == NULL || big == NULL, "malloc");

    printf("%-24s %14s   %s  %s\n", "zone", "address", "perm", "region");
    show("code   (main)", (const void *)main);
    show("data   (initialized)", &initialized);
    show("bss    (global_array)", global_array);
    show("heap   (malloc 10 B)", small);
    show("heap   (end: sbrk(0))", (char *)sbrk(0) - 1);
    show("mmap   (malloc 64 MiB)", big);
    show("libc   (printf)", (const void *)printf);
    show("stack  (local)", &local);

    free(big);
    free(small);
    return EXIT_SUCCESS;
}
