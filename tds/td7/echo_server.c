/*
 * TD7 - exercise 2: echo server. Sends back every line it receives; "quit" closes the
 * connection and the server waits for the next client.
 *   ./echo_server [port] [-f]      -f: one child process per client (fork)
 * Without -f the server is ITERATIVE: a second telnet waits until the first one leaves
 * (its connection stays in the listen queue). With -f each client gets its own process.
 */
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#define PORT 7000
#define LINE 1024

/* Echo line by line until end of file or "quit". Returns when the client is done. */
static void serve(int c)
{
    char line[LINE];
    size_t len = 0;

    for (;;) {
        ssize_t n = read(c, line + len, 1);          /* byte by byte: simple, fine for a lab */
        if (n == -1 && errno == EINTR)
            continue;
        if (n <= 0)                                   /* 0: the client closed; -1: error */
            return;
        if (line[len] == '\n' || len == LINE - 2) {
            len++;
            if (strncmp(line, "quit", 4) == 0)
                return;
            if (write_all(c, line, len) == -1)
                return;
            len = 0;
        } else {
            len++;
        }
    }
}

int main(int argc, char *argv[])
{
    int port = PORT, use_fork = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) use_fork = 1;
        else port = atoi(argv[i]);
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(s == -1, "socket");
    int yes = 1;
    exit_if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1, "setsockopt");
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    exit_if(bind(s, (struct sockaddr *)&addr, sizeof addr) == -1, "bind");
    exit_if(listen(s, 8) == -1, "listen");
    printf("echo server on port %d (%s)\n", port, use_fork ? "one process per client" : "iterative");
    fflush(stdout);

    /* Children that finish must not stay zombies: ignoring SIGCHLD makes the kernel reap them. */
    if (use_fork)
        exit_if(signal(SIGCHLD, SIG_IGN) == SIG_ERR, "signal");

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c == -1 && errno == EINTR)
            continue;
        exit_if(c == -1, "accept");

        if (!use_fork) {
            serve(c);
            exit_if(close(c) == -1, "close");
            continue;
        }
        pid_t pid = fork();
        exit_if(pid == -1, "fork");
        if (pid == 0) {
            exit_if(close(s) == -1, "close");         /* the child does not accept */
            serve(c);
            _exit(EXIT_SUCCESS);
        }
        exit_if(close(c) == -1, "close");             /* the parent does not talk to the client */
    }
}
