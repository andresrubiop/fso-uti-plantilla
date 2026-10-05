/*
 * TD1 - exercise 8 (answers): size and padding of struct nopad, and two ways to shrink it.
 * The compiler aligns `long` on 8 bytes: 7 padding bytes after c1 and 7 after c2 (arrays of
 * the structure must keep every `l` aligned). -O3 does not change the layout: it is part of
 * the ABI, not an optimisation.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

struct reordered {          /* biggest member first: only 6 padding bytes at the end */
    long l;
    char c1;
    char c2;
};

struct __attribute__((packed)) packed {   /* no padding at all (slower, unaligned access) */
    char c1;
    long l;
    char c2;
};

int main(void)
{
    printf("struct nopad     : size %2zu  offsets c1=%zu l=%zu c2=%zu\n", sizeof(struct nopad),
           offsetof(struct nopad, c1), offsetof(struct nopad, l), offsetof(struct nopad, c2));
    printf("struct reordered : size %2zu  offsets l=%zu c1=%zu c2=%zu\n", sizeof(struct reordered),
           offsetof(struct reordered, l), offsetof(struct reordered, c1), offsetof(struct reordered, c2));
    printf("struct packed    : size %2zu  offsets c1=%zu l=%zu c2=%zu\n", sizeof(struct packed),
           offsetof(struct packed, c1), offsetof(struct packed, l), offsetof(struct packed, c2));
    return EXIT_SUCCESS;
}
