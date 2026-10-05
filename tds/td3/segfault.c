/*
 * TD3 - exercise 4: a program that dies with SIGSEGV, to test the error path of launch.
 * Address 0 is never mapped: the MMU raises a page fault, the kernel finds no mapping and
 * sends SIGSEGV to the process.
 */
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

static void gen_segfault(void)
{
    volatile int *p = NULL;     /* volatile: the compiler must really do the store */
    *p = 42;
}

int main(void)
{
    printf("about to write at address 0...\n");
    fflush(stdout);
    gen_segfault();
    return EXIT_SUCCESS;        /* never reached */
}
