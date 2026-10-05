/*
 * TD1 - bench_read: how much does a system call cost? Copies FILE to /dev/null with read/write
 * and buffers from 1 byte to 1 MiB, and prints one CSV line per size:
 *   buffer_bytes, read_calls, seconds, MB_per_s, ns_per_iteration   (one iteration = read + write)
 *   ./bench_read [file]        (default: an 8 MiB file created in the current directory)
 * The file is read once before measuring so that it is in the page cache: we measure the cost of
 * the calls and the copies, not the disk.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

#define DEFAULT_SIZE (8L << 20)

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Copies fd to /dev/null with a buffer of `size` bytes; returns the number of read calls. */
static long copy(int fd, int out, char *buf, size_t size)
{
    long calls = 0;
    ssize_t n;
    exit_if(lseek(fd, 0, SEEK_SET) == -1, "lseek");
    do {
        n = read(fd, buf, size);
        exit_if(n == -1, "read");
        calls++;
        if (n > 0)
            exit_if(write_all(out, buf, (size_t)n) == -1, "write");
    } while (n > 0);
    return calls;
}

int main(int argc, char *argv[])
{
    const char *path = argc > 1 ? argv[1] : "bench.dat";
    if (argc == 1) {                                  /* create the test file */
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        exit_if(fd == -1, path);
        char block[65536];
        memset(block, 'x', sizeof block);
        for (long done = 0; done < DEFAULT_SIZE; done += (long)sizeof block)
            exit_if(write_all(fd, block, sizeof block) == -1, "write");
        exit_if(close(fd) == -1, "close");
    }
    int fd = open(path, O_RDONLY);
    exit_if(fd == -1, path);
    int out = open("/dev/null", O_WRONLY);
    exit_if(out == -1, "/dev/null");
    off_t bytes = lseek(fd, 0, SEEK_END);
    exit_if(bytes <= 0, "empty file");

    char *buf = malloc(1 << 20);
    exit_if(buf == NULL, "malloc");
    copy(fd, out, buf, 1 << 20);                      /* warm-up: file in the page cache */

    printf("buffer_bytes,read_calls,seconds,MB_per_s,ns_per_iteration\n");
    for (size_t size = 1; size <= (1 << 20); size *= 4) {
        double t0 = now();
        long calls = copy(fd, out, buf, size);
        double dt = now() - t0;
        printf("%zu,%ld,%.4f,%.1f,%.0f\n", size, calls, dt, (double)bytes / dt / 1e6, dt / (double)calls * 1e9);
        fflush(stdout);
    }
    free(buf);
    close(fd);
    close(out);
    return EXIT_SUCCESS;
}
