/*
 * TD7 - exploration: addresses and ports, the modern way.
 *   ./addr [port]      (default 7000)
 * 1. The bug of the original slides: sin_port = 7000 without htons. On a little-endian CPU
 *    (x86-64, ARM64) the kernel reads the two bytes the other way round and binds ANOTHER port.
 * 2. Name resolution with getaddrinfo (gethostbyname is obsolete: IPv4 only, not thread-safe,
 *    removed from POSIX in 2008). "localhost" and "http" come from /etc/hosts and /etc/services;
 *    the same call handles IPv6.
 * 3. Ports below net.ipv4.ip_unprivileged_port_start (1024) need privileges.
 * 4. The sizes of the address structures: every one is passed as a struct sockaddr *.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "utils.h"

/* Binds a TCP socket to 127.0.0.1 with sin_port = raw (as given) and prints the real port. */
static void try_port(const char *label, uint16_t raw)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(s == -1, "socket");
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = raw };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&a, sizeof a) == -1) {
        printf("%-26s bind: %s\n", label, strerror(errno));
    } else {
        socklen_t len = sizeof a;
        exit_if(getsockname(s, (struct sockaddr *)&a, &len) == -1, "getsockname");
        printf("%-26s the kernel bound port %u\n", label, ntohs(a.sin_port));
    }
    exit_if(close(s) == -1, "close");
}

int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 7000;
    char label[64];

    printf("port %d = 0x%04x; htons(%d) = 0x%04x in memory order\n", port, port, port,
           htons((uint16_t)port));
    snprintf(label, sizeof label, "sin_port = %d:", port);
    try_port(label, (uint16_t)port);                       /* the bug of the original */
    snprintf(label, sizeof label, "sin_port = htons(%d):", port);
    try_port(label, htons((uint16_t)port));

    const char *names[][2] = { { "localhost", "http" }, { "::1", "https" } };
    for (int k = 0; k < 2; k++) {
        struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *res;
        int e = getaddrinfo(names[k][0], names[k][1], &hints, &res);
        exit_if(e != 0, gai_strerror(e));
        for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
            char host[INET6_ADDRSTRLEN];
            int v6 = p->ai_family == AF_INET6;
            const void *ip = v6 ? (const void *)&((struct sockaddr_in6 *)p->ai_addr)->sin6_addr
                                : (const void *)&((struct sockaddr_in *)p->ai_addr)->sin_addr;
            uint16_t pt = v6 ? ((struct sockaddr_in6 *)p->ai_addr)->sin6_port
                             : ((struct sockaddr_in *)p->ai_addr)->sin_port;
            exit_if(inet_ntop(p->ai_family, ip, host, sizeof host) == NULL, "inet_ntop");
            printf("getaddrinfo(\"%s\", \"%s\"): %s %s port %u\n", names[k][0], names[k][1],
                   v6 ? "IPv6" : "IPv4", host, ntohs(pt));
        }
        freeaddrinfo(res);
    }

    FILE *f = fopen("/proc/sys/net/ipv4/ip_unprivileged_port_start", "r");
    int start = -1;
    if (f != NULL) {
        exit_if(fscanf(f, "%d", &start) != 1, "fscanf");
        fclose(f);
    }
    printf("ip_unprivileged_port_start = %d\n", start);
    try_port("sin_port = htons(80):", htons(80));

    printf("sizeof: sockaddr %zu, sockaddr_in %zu, sockaddr_in6 %zu, sockaddr_un %zu, sockaddr_storage %zu\n",
           sizeof(struct sockaddr), sizeof(struct sockaddr_in), sizeof(struct sockaddr_in6),
           sizeof(struct sockaddr_un), sizeof(struct sockaddr_storage));
    return EXIT_SUCCESS;
}
