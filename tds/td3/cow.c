/*
 * TD3 - exploration 3: copy-on-write after fork (original slide 74).
 * The parent fills a block, so all its pages are resident. After fork, parent and child share the
 * physical pages, marked read-only. The child reads the whole block (no copy: no page faults) and then
 * writes to a quarter of the pages: each first write causes a page fault in which the kernel copies
 * that single page. The page faults are counted with getrusage.
 *   ./cow [MiB]        default: 256 MiB
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

static long minor_faults(void)
{
    struct rusage ru;
    exit_if(getrusage(RUSAGE_SELF, &ru) == -1, "getrusage");
    return ru.ru_minflt;
}

static double now(void)
{
    struct timespec ts;
    exit_if(clock_gettime(CLOCK_MONOTONIC, &ts) == -1, "clock_gettime");
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char *argv[])
{
    size_t mib = argc > 1 ? (size_t)atol(argv[1]) : 256;
    size_t size = mib << 20, page = (size_t)sysconf(_SC_PAGESIZE), pages = size / page;
    char *p = malloc(size);
    exit_if(p == NULL, "malloc");
    memset(p, 'x', size);                                  /* every page is now resident */
    printf("parent: %zu MiB filled (%zu pages of %zu bytes)\n", mib, pages, page);
    fflush(stdout);                                        /* do not duplicate the stdio buffer */

    double t0 = now();
    pid_t pid = fork();
    exit_if(pid == -1, "fork");
    if (pid == 0) {
        printf("fork took %.2f ms (only the page tables are copied)\n", (now() - t0) * 1e3);

        long f = minor_faults();
        unsigned long sum = 0;
        for (size_t i = 0; i < size; i += page)
            sum += (unsigned char)p[i];
        printf("child reads  %zu pages: %ld page faults (shared, nothing copied; sum %lu)\n", pages, minor_faults() - f, sum);

        f = minor_faults();
        double t1 = now();
        for (size_t i = 0; i < size / 4; i += page)
            p[i] = 'y';
        double t2 = now();
        long faults = minor_faults() - f;
        printf("child writes %zu pages: %ld page faults (one copy per page), %.0f ns per copy\n",
               pages / 4, faults, faults ? (t2 - t1) * 1e9 / faults : 0.0);
        free(p);
        exit(EXIT_SUCCESS);
    }
    int status;
    exit_if(waitpid(pid, &status, 0) == -1, "waitpid");
    printf("parent: the child exited with status %d; my copy still says '%c'\n", WEXITSTATUS(status), p[0]);
    free(p);
    return EXIT_SUCCESS;
}
