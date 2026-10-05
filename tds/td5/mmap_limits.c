/*
 * TD5 - exercise 2, last question: how big is the shared segment of a 128-byte file?
 * The kernel maps whole pages. We map TWO pages of a 128-byte file and write byte by byte:
 *   bytes 0..127      belong to the file
 *   bytes 128..4095   exist (same page) but are never written back to the file
 *   byte 4096         is in a page that lies entirely past the end of the file: SIGBUS
 * The signal is caught and siglongjmp brings us back to report where it happened.
 */
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "utils.h"

#define FILENAME "limits.txt"
#define FILESIZE 128

static sigjmp_buf env;
static volatile sig_atomic_t caught;
static char *volatile map;              /* volatile: must survive the siglongjmp */
static volatile long offset;

static void on_fault(int sig)
{
    caught = sig;
    siglongjmp(env, 1);
}

int main(void)
{
    const long page = sysconf(_SC_PAGESIZE);
    int fd = open(FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0644);
    exit_if(fd == -1, "open");
    exit_if(ftruncate(fd, FILESIZE) == -1, "ftruncate");
    map = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    exit_if(map == MAP_FAILED, "mmap");

    struct sigaction sa = { 0 };
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    exit_if(sigaction(SIGSEGV, &sa, NULL) == -1 || sigaction(SIGBUS, &sa, NULL) == -1, "sigaction");

    if (sigsetjmp(env, 1) == 0)
        for (offset = 0; offset < 2 * page; offset++)
            map[offset] = 'x';

    printf("file size %d bytes, page size %ld bytes, mapping of %ld bytes\n", FILESIZE, page, 2 * page);
    printf("wrote bytes 0..%ld; byte %ld raised %s\n", offset - 1, offset,
           caught ? signame(caught) : "nothing");

    exit_if(munmap(map, 2 * page) == -1, "munmap");
    off_t end = lseek(fd, 0, SEEK_END);               /* the file did not grow */
    exit_if(end == -1, "lseek");
    printf("file size after the writes: %lld bytes\n", (long long)end);
    exit_if(close(fd) == -1, "close");
    return EXIT_SUCCESS;
}
