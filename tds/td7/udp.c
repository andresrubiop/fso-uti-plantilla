/*
 * TD7 - exploration: a stream (TCP) versus datagrams (UDP), over 127.0.0.1.
 *   ./udp
 * 1. Three writes on a TCP connection can arrive in ONE read: TCP carries bytes, not messages.
 * 2. Three sendto on UDP arrive as three recvfrom: each datagram keeps its boundaries.
 * 3. UDP does not wait for the receiver: if its buffer is full the kernel drops datagrams and
 *    nobody gets an error (not even on the same machine). The kernel counts them in
 *    /proc/net/snmp (Udp: RcvbufErrors).
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#define SENT 10000

static struct sockaddr_in loopback(void)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = 0 };   /* any free port */
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}

/* Binds s to 127.0.0.1 on a free port and returns the address it got. */
static struct sockaddr_in bind_any(int s)
{
    struct sockaddr_in a = loopback();
    socklen_t len = sizeof a;
    exit_if(bind(s, (struct sockaddr *)&a, sizeof a) == -1, "bind");
    exit_if(getsockname(s, (struct sockaddr *)&a, &len) == -1, "getsockname");
    return a;
}

/* The RcvbufErrors counter of the "Udp:" line of /proc/net/snmp (-1 if not found). */
static long rcvbuf_errors(void)
{
    FILE *f = fopen("/proc/net/snmp", "r");
    exit_if(f == NULL, "/proc/net/snmp");
    char names[1024], values[1024];
    long result = -1;
    while (fgets(names, sizeof names, f) && fgets(values, sizeof values, f)) {
        if (strncmp(names, "Udp:", 4) != 0)
            continue;
        char *sn, *sv, *n = strtok_r(names, " \n", &sn), *v = strtok_r(values, " \n", &sv);
        while (n && v) {
            if (strcmp(n, "RcvbufErrors") == 0)
                result = atol(v);
            n = strtok_r(NULL, " \n", &sn);
            v = strtok_r(NULL, " \n", &sv);
        }
        break;
    }
    fclose(f);
    return result;
}

int main(void)
{
    const char *msg[] = { "one", "two", "three" };
    char buf[2048];

    /* 1. TCP: three writes, one read. */
    int l = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(l == -1, "socket");
    struct sockaddr_in a = bind_any(l);
    exit_if(listen(l, 1) == -1, "listen");
    int c = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(c == -1, "socket");
    exit_if(connect(c, (struct sockaddr *)&a, sizeof a) == -1, "connect");
    int srv = accept(l, NULL, NULL);
    exit_if(srv == -1, "accept");
    for (int i = 0; i < 3; i++)
        exit_if(write_all(c, msg[i], strlen(msg[i])) == -1, "write");
    usleep(50000);                                         /* let everything arrive */
    ssize_t n = read(srv, buf, sizeof buf - 1);
    exit_if(n == -1, "read");
    buf[n] = '\0';
    printf("TCP: 3 writes (one, two, three) -> read #1 returns %zd bytes: \"%s\"\n", n, buf);
    exit_if(close(c) == -1 || close(srv) == -1 || close(l) == -1, "close");

    /* 2. UDP: three sendto, three recvfrom. */
    int r = socket(AF_INET, SOCK_DGRAM, 0), s = socket(AF_INET, SOCK_DGRAM, 0);
    exit_if(r == -1 || s == -1, "socket");
    a = bind_any(r);
    for (int i = 0; i < 3; i++)
        exit_if(sendto(s, msg[i], strlen(msg[i]), 0, (struct sockaddr *)&a, sizeof a) == -1, "sendto");
    printf("UDP: 3 sendto  (one, two, three) ->");
    for (int i = 0; i < 3; i++) {
        n = recvfrom(r, buf, sizeof buf - 1, 0, NULL, NULL);
        exit_if(n == -1, "recvfrom");
        buf[n] = '\0';
        printf(" recvfrom #%d: %zd \"%s\"%s", i + 1, n, buf, i < 2 ? "," : "\n");
    }

    /* 3. UDP loss: a small receive buffer that nobody reads while 10000 datagrams arrive. */
    int small = 4096;                                      /* the kernel doubles it (socket(7)) */
    exit_if(setsockopt(r, SOL_SOCKET, SO_RCVBUF, &small, sizeof small) == -1, "setsockopt");
    socklen_t len = sizeof small;
    exit_if(getsockopt(r, SOL_SOCKET, SO_RCVBUF, &small, &len) == -1, "getsockopt");
    long before = rcvbuf_errors();
    memset(buf, 'x', 100);
    int sent = 0;
    for (int i = 0; i < SENT; i++)
        if (sendto(s, buf, 100, 0, (struct sockaddr *)&a, sizeof a) == 100)
            sent++;                                        /* sendto succeeds every time */
    int got = 0;
    while (recv(r, buf, sizeof buf, MSG_DONTWAIT) > 0)
        got++;
    exit_if(errno != EAGAIN && errno != EWOULDBLOCK, "recv");
    long after = rcvbuf_errors();
    printf("UDP: receive buffer %d bytes, %d sendto of 100 bytes succeeded, %d received, %d lost\n",
           small, sent, got, sent - got);
    printf("     kernel counter Udp RcvbufErrors grew by %ld\n", after - before);
    exit_if(close(r) == -1 || close(s) == -1, "close");
    return EXIT_SUCCESS;
}
