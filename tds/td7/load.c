/*
 * TD7 - exploration: the server models under load.
 *   ./load port [slow] [fast] [slow_ms]      (default 1 slow, 31 fast, 1000 ms)
 * `slow` clients connect first and wait slow_ms before sending their request (a client on a
 * bad network, or an attacker keeping connections busy). 20 ms later the `fast` clients
 * connect and send at once. Prints the latency of each group (connect -> whole reply).
 * A server that is busy waiting for a slow client makes the fast ones wait too.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

#define MAXC 256

static int port, slow, slow_ms;
static double latency[MAXC];
static int ok[MAXC];

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *client(void *arg)
{
    int id = (int)(intptr_t)arg, is_slow = id < slow;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(s == -1, "socket");

    if (!is_slow)
        usleep(20000);                                    /* the slow ones are already inside */
    double t = now();
    exit_if(connect(s, (struct sockaddr *)&addr, sizeof addr) == -1, "connect");
    if (is_slow)
        usleep((useconds_t)slow_ms * 1000);
    const char *req = "GET / HTTP/1.0\r\n\r\n";
    exit_if(write_all(s, req, strlen(req)) == -1, "write");

    char buf[4096], status[13] = "";                      /* the first 12 bytes of the reply */
    size_t got = 0;
    ssize_t n;
    while ((n = read(s, buf, sizeof buf)) > 0)
        for (ssize_t i = 0; i < n && got < 12; i++)
            status[got++] = buf[i];
    exit_if(n == -1, "read");
    ok[id] = strcmp(status, "HTTP/1.0 200") == 0;
    latency[id] = now() - t;
    exit_if(close(s) == -1, "close");
    return NULL;
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Median and maximum of latency[from..to), in milliseconds. */
static void stats(const char *name, int from, int to)
{
    if (to <= from)
        return;
    qsort(latency + from, (size_t)(to - from), sizeof latency[0], cmp);
    printf("  %s %2d: median %6.0f ms, max %6.0f ms\n", name, to - from,
           latency[from + (to - from) / 2] * 1000, latency[to - 1] * 1000);
}

int main(int argc, char *argv[])
{
    exit_if(argc < 2, "usage: load port [slow] [fast] [slow_ms]");
    port = atoi(argv[1]);
    slow = argc > 2 ? atoi(argv[2]) : 1;
    int fast = argc > 3 ? atoi(argv[3]) : 31;
    slow_ms = argc > 4 ? atoi(argv[4]) : 1000;
    int n = slow + fast;
    exit_if(slow < 0 || fast < 0 || n < 1 || n > MAXC, "clients: 1..256");

    pthread_t t[MAXC];
    for (int i = 0; i < n; i++)
        exit_if(pthread_create(&t[i], NULL, client, (void *)(intptr_t)i) != 0, "pthread_create");
    for (int i = 0; i < n; i++)
        exit_if(pthread_join(t[i], NULL) != 0, "pthread_join");

    int good = 0;
    for (int i = 0; i < n; i++)
        good += ok[i];
    printf("%d replies 200 OK of %d\n", good, n);
    stats("slow", 0, slow);
    stats("fast", slow, n);
    return EXIT_SUCCESS;
}
