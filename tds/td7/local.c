/*
 * TD7 - exploration: a local socket (AF_UNIX). Its address is a PATH in the file system, not
 * 127.0.0.1 (that is AF_INET over the loopback interface). The kernel also tells the server
 * who the client is (SO_PEERCRED): systemd, D-Bus or Docker use it to decide what to allow.
 *   ./local [path]      (default fso.sock)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "utils.h"

int main(int argc, char *argv[])
{
    const char *path = argc > 1 ? argv[1] : "fso.sock";
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    exit_if(strlen(path) >= sizeof a.sun_path, "path too long for sun_path");
    strcpy(a.sun_path, path);

    int l = socket(AF_UNIX, SOCK_STREAM, 0);
    exit_if(l == -1, "socket");
    exit_if(unlink(path) == -1 && access(path, F_OK) == 0, "unlink");   /* left by a previous run */
    exit_if(bind(l, (struct sockaddr *)&a, sizeof a) == -1, "bind");
    exit_if(listen(l, 1) == -1, "listen");

    struct stat st;
    exit_if(stat(path, &st) == -1, "stat");
    printf("bind created %s: a file of type %s (the 's' of ls -l), sun_path holds %zu bytes\n",
           path, S_ISSOCK(st.st_mode) ? "socket" : "?", sizeof a.sun_path);

    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    exit_if(c == -1, "socket");
    exit_if(connect(c, (struct sockaddr *)&a, sizeof a) == -1, "connect");
    int srv = accept(l, NULL, NULL);
    exit_if(srv == -1, "accept");

    struct ucred who;
    socklen_t len = sizeof who;
    exit_if(getsockopt(srv, SOL_SOCKET, SO_PEERCRED, &who, &len) == -1, "getsockopt");
    printf("the server asks the kernel who connected: pid %s, uid %u%s\n",
           who.pid == getpid() ? "= mine" : "other", (unsigned)who.uid,
           who.uid == getuid() ? " (my user)" : "");

    char buf[64];
    exit_if(write_all(c, "hello", 5) == -1, "write");
    ssize_t n = read(srv, buf, sizeof buf);
    exit_if(n == -1, "read");
    printf("read %zd bytes; after close the file stays until unlink\n", n);

    exit_if(close(c) == -1 || close(srv) == -1 || close(l) == -1, "close");
    printf("still there after close: %s\n", access(path, F_OK) == 0 ? "yes" : "no");
    exit_if(unlink(path) == -1, "unlink");
    return EXIT_SUCCESS;
}
