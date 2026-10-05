/*
 * TD3 - exploration 2: lazy allocation (original slides 59 and 61).
 * malloc of a large block returns at once and costs almost no physical memory: the kernel only
 * reserves virtual addresses. Each page gets a physical frame the first time it is touched, through a
 * (minor) page fault. The program measures the resident memory (VmRSS) and the page faults
 * (getrusage) before and after touching half of the block.
 *   ./lazy [MiB]        default: 1024 MiB
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

static long minor_faults(void)
{
    struct rusage ru;
    exit_if(getrusage(RUSAGE_SELF, &ru) == -1, "getrusage");
    return ru.ru_minflt;
}

/* Resident set size in KiB, from /proc/self/status. */
static long rss_kib(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    exit_if(f == NULL, "/proc/self/status");
    char line[256];
    long kib = -1;
    while (fgets(line, sizeof line, f) != NULL)
        if (sscanf(line, "VmRSS: %ld kB", &kib) == 1)
            break;
    exit_if(fclose(f) == EOF, "fclose");
    return kib;
}

static double now(void)
{
    struct timespec ts;
    exit_if(clock_gettime(CLOCK_MONOTONIC, &ts) == -1, "clock_gettime");
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void report(const char *step, long f0)
{
    printf("%-34s VmRSS = %8ld KiB   page faults so far = %ld\n", step, rss_kib(), minor_faults() - f0);
}

int main(int argc, char *argv[])
{
    size_t mib = argc > 1 ? (size_t)atol(argv[1]) : 1024;
    size_t size = mib << 20, page = (size_t)sysconf(_SC_PAGESIZE);
    long f0 = minor_faults();

    report("start", f0);
    char *p = malloc(size);
    exit_if(p == NULL, "malloc");
    printf("malloc(%zu MiB) returned %p\n", mib, (void *)p);
    report("after malloc", f0);

    long before = minor_faults();
    double t0 = now();
    for (size_t i = 0; i < size / 2; i += page)     /* touch one byte of every page of the first half */
        p[i] = 1;
    double t1 = now();
    long faults = minor_faults() - before;
    report("after touching half of the pages", f0);
    printf("pages touched: %zu (%zu bytes each)   page faults: %ld   %.0f ns per fault\n",
           size / 2 / page, page, faults, faults ? (t1 - t0) * 1e9 / faults : 0.0);

    free(p);
    report("after free", f0);
    return EXIT_SUCCESS;
}
