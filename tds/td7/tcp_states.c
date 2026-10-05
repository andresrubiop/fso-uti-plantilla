/*
 * TD7 - exploration: the TCP state machine, read from the kernel with getsockopt(TCP_INFO).
 *   ./tcp_states [port]      (default 7001, on 127.0.0.1)
 * One process plays both sides: a listening socket, a client and the connection the server
 * accepts. The client closes first, so it is the side that ends in TIME_WAIT.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

static const char *NAMES[] = { "?", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
                               "TIME_WAIT", "CLOSED", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING" };

static struct tcp_info info(int s)
{
    struct tcp_info ti;
    socklen_t len = sizeof ti;
    exit_if(getsockopt(s, IPPROTO_TCP, TCP_INFO, &ti, &len) == -1, "getsockopt");
    return ti;
}

static const char *state(int s)
{
    if (s == -1)
        return "-";
    unsigned st = info(s).tcpi_state;
    return st < sizeof NAMES / sizeof NAMES[0] ? NAMES[st] : "?";
}

/* One line per step: the state of each socket. For a listening socket the kernel reports the
 * accept queue in tcpi_unacked (connections waiting for accept) and tcpi_sacked (backlog). */
static void show(const char *step, int l, int c, int a)
{
    struct tcp_info li = info(l);
    printf("%-26s | listener %s (queue %u/%u) | client %-11s | server %s\n",
           step, state(l), li.tcpi_unacked, li.tcpi_sacked, state(c), state(a));
}

int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 7001;
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int l = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(l == -1, "socket");
    int yes = 1;
    exit_if(setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1, "setsockopt");
    exit_if(bind(l, (struct sockaddr *)&addr, sizeof addr) == -1, "bind");
    exit_if(listen(l, 4) == -1, "listen");
    show("socket+bind+listen(4)", l, -1, -1);

    int c = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(c == -1, "socket");
    exit_if(connect(c, (struct sockaddr *)&addr, sizeof addr) == -1, "connect");
    show("connect (no accept yet)", l, c, -1);           /* the handshake is done by the kernel */

    int a = accept(l, NULL, NULL);
    exit_if(a == -1, "accept");
    show("accept", l, c, a);

    char buf[64];
    exit_if(write_all(c, "hello\n", 6) == -1, "write");
    ssize_t n = read(a, buf, sizeof buf);
    exit_if(n == -1, "read");
    printf("%-26s | server read %zd bytes\n", "write/read", n);

    exit_if(shutdown(c, SHUT_WR) == -1, "shutdown");     /* the client sends FIN */
    usleep(100000);                                      /* the server's ACK may wait 40 ms */
    show("client shutdown (FIN)", l, c, a);

    n = read(a, buf, sizeof buf);
    exit_if(n == -1, "read");
    printf("%-26s | server read returns %zd: end of file\n", "server read", n);

    exit_if(shutdown(a, SHUT_WR) == -1, "shutdown");     /* the server sends its FIN */
    usleep(100000);
    show("server shutdown (FIN)", l, c, a);
    printf("%-26s | the client's end moved to a TIME_WAIT entry of the kernel (60 s, see ss)\n", "");

    exit_if(close(a) == -1 || close(c) == -1 || close(l) == -1, "close");
    return EXIT_SUCCESS;
}
