/*
 * TD3 - exercise 2: the three buffering modes of the C library.
 * One source, several behaviours chosen at compile time (see the Makefile variants):
 *   STRING1, STRING2        the two strings (default "Hello " and "World!\n")
 *   USE_FPRINTF1/2          write string 1/2 with fprintf (buffered) instead of write (not buffered)
 *   USE_STDERR1/2           write string 1/2 on stderr instead of stdout
 *   USE__EXIT               end with _exit() (no buffer flush) instead of exit()
 * Run each variant twice:  ./buffer_N   (terminal)   and   ./buffer_N 2>&1 | cat -u   (pipe)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#ifndef STRING1
#define STRING1 "Hello "
#endif
#ifndef STRING2
#define STRING2 "World!\n"
#endif

#ifdef USE_STDERR1
#define STREAM1 stderr
#define FD1 STDERR_FILENO
#else
#define STREAM1 stdout
#define FD1 STDOUT_FILENO
#endif

#ifdef USE_STDERR2
#define STREAM2 stderr
#define FD2 STDERR_FILENO
#else
#define STREAM2 stdout
#define FD2 STDOUT_FILENO
#endif

int main(void)
{
#ifdef USE_FPRINTF1
    fputs(STRING1, STREAM1);                                  /* goes to the stdio buffer */
#else
    exit_if(write(FD1, STRING1, strlen(STRING1)) == -1, "write");   /* straight to the kernel */
#endif

    sleep(1);

#ifdef USE_FPRINTF2
    fputs(STRING2, STREAM2);
#else
    exit_if(write(FD2, STRING2, strlen(STRING2)) == -1, "write");
#endif

#ifdef USE__EXIT
    _exit(EXIT_SUCCESS);        /* system call: the stdio buffers are NOT flushed */
#else
    exit(EXIT_SUCCESS);         /* library function: flushes the stdio buffers, then _exit */
#endif
}
