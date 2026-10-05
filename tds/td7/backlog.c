/*
 * TD7 - exploration: what the backlog of listen() really limits.
 *   ./backlog [backlog] [clients]      (default 2 and 8, on 127.0.0.1)
 * The server never calls accept at first. The kernel completes the three-way handshake of
 * backlog + 1 clients and puts them in the accept queue; the SYN of the others is dropped,
 * so they stay in SYN_SENT and retransmit it (after 1 s, then 3 s, 7 s...). When the server
 * accepts, the queue empties and the next retransmitted SYN gets in.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#define MAXC 64

static unsigned tcp_state(int s, unsigned *queue)
{
    struct tcp_info ti;
    socklen_t len = sizeof ti;
    exit_if(getsockopt(s, IPPROTO_TCP, TCP_INFO, &ti, &len) == -1, "getsockopt");
    if (queue != NULL)
        *queue = ti.tcpi_unacked;                        /* listening socket: accept queue */
    return ti.tcpi_state;
}

static void count(const char *when, int l, const int *c, int n)
{
    int est = 0, syn = 0;
    unsigned queue;
    for (int i = 0; i < n; i++) {
        unsigned st = tcp_state(c[i], NULL);
        est += st == TCP_ESTABLISHED;
        syn += st == TCP_SYN_SENT;
    }
    tcp_state(l, &queue);
    printf("%-30s accept queue %u | clients ESTABLISHED %d, SYN_SENT %d\n", when, queue, est, syn);
}

int main(int argc, char *argv[])
{
    int backlog = argc > 1 ? atoi(argv[1]) : 2;
    int n = argc > 2 ? atoi(argv[2]) : 8;
    exit_if(n < 1 || n > MAXC, "clients: 1..64");

    int l = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);   /* accept returns EAGAIN if empty */
    exit_if(l == -1, "socket");
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = 0 };   /* any free port */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    exit_if(bind(l, (struct sockaddr *)&addr, sizeof addr) == -1, "bind");
    socklen_t alen = sizeof addr;
    exit_if(getsockname(l, (struct sockaddr *)&addr, &alen) == -1, "getsockname");
    exit_if(listen(l, backlog) == -1, "listen");

    int c[MAXC] = { 0 };
    for (int i = 0; i < n; i++) {                        /* non-blocking: connect returns at once */
        c[i] = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        exit_if(c[i] == -1, "socket");
        int r = connect(c[i], (struct sockaddr *)&addr, sizeof addr);
        exit_if(r == -1 && errno != EINPROGRESS, "connect");
    }
    printf("listen(%d), %d clients connect\n", backlog, n);
    usleep(300000);
    count("0.3 s, no accept yet:", l, c, n);

    /* Rounds: accept everything that is queued, then wait past the next retransmission. */
    int acc[MAXC], accepted = 0;                         /* kept open until the end */
    for (int round = 1; accepted < n && round <= 3; round++) {
        int now = 0, a;
        while (accepted < n && (a = accept(l, NULL, NULL)) != -1) {
            acc[accepted++] = a;
            now++;
        }
        exit_if(accepted < n && errno != EAGAIN && errno != EWOULDBLOCK, "accept");
        char when[48];
        snprintf(when, sizeof when, "accept %d (total %d), wait:", now, accepted);
        if (accepted < n) {
            usleep(round == 1 ? 1500000 : 2500000);
            count(when, l, c, n);
        } else {
            printf("%s all %d clients accepted\n", when, n);
        }
    }

    for (int i = 0; i < n; i++)
        exit_if(close(c[i]) == -1, "close");
    for (int i = 0; i < accepted; i++)
        exit_if(close(acc[i]) == -1, "close");
    exit_if(close(l) == -1, "close");
    return EXIT_SUCCESS;
}
