/*
 * TD7 - exercise 1: the server side of a TCP connection, step by step.
 *   ./sock [port]      (default 7000)   then, from another terminal:   telnet localhost 7000
 * socket -> bind -> listen -> accept -> close.
 */
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"

#define PORT 7000

int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : PORT;

    /* 1. An IPv4 (AF_INET), connected, reliable byte stream (SOCK_STREAM = TCP). */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(s == -1, "socket");

    /* Allow restarting the server right away (otherwise bind fails for ~60 s: TIME_WAIT). */
    int yes = 1;
    exit_if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1, "setsockopt");

    /* 2. Local address: any interface of this machine, the chosen port (network byte order). */
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    /* 3. and 4. Bind the address, start listening (queue of 1 pending connection). */
    exit_if(bind(s, (struct sockaddr *)&addr, sizeof addr) == -1, "bind");
    exit_if(listen(s, 1) == -1, "listen");
    printf("listening on port %d\n", port);
    fflush(stdout);

    /* 5. accept blocks until a client connects and returns a NEW descriptor for it. */
    int client = accept(s, NULL, NULL);
    exit_if(client == -1, "accept");
    printf("connection accepted, closing\n");

    /* 6. Close the connection and the listening socket. */
    exit_if(close(client) == -1 || close(s) == -1, "close");
    return EXIT_SUCCESS;
}
