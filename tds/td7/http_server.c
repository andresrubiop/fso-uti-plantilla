/*
 * TD7 - exercise 3: a minimal web server (HTTP/1.0, GET only) in three versions.
 *   ./http_server [-p port] [-m iterative|fork|thread|pool|epoll] [-d root]
 *     iterative  one client at a time
 *     fork       one child process per connection
 *     thread     one new thread per connection
 *     pool       8 threads created at start; the main thread PRODUCES connections into a
 *                bounded queue, the workers CONSUME them (producer/consumer of TD6)
 *     epoll      one thread and an event loop: epoll_wait says which sockets have data
 *                (the model of nginx or Node.js; a real one also writes without blocking)
 * The file is found with stat(2), mapped with mmap(2) and sent with write(2).
 * Paths containing ".." are refused: the client must not escape the served directory.
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

#define REQUEST 2048
#define WORKERS 8
#define QUEUE   32

static const char *root = ".";

static const char *mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) return "application/octet-stream";
    if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".txt") || !strcmp(dot, ".c"))   return "text/plain; charset=utf-8";
    if (!strcmp(dot, ".css"))  return "text/css";
    if (!strcmp(dot, ".js"))   return "text/javascript";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    return "application/octet-stream";
}

static void reply_error(int c, const char *status)
{
    char msg[256];
    int n = snprintf(msg, sizeof msg,
                     "HTTP/1.0 %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s\n",
                     status, strlen(status) + 1, status);
    (void)write_all(c, msg, (size_t)n);                /* the client may be gone: ignore */
}

/* Handles one request on the connected socket c. Never exits the process. */
static void handle(int c)
{
    char req[REQUEST], path[1024], full[2048];
    ssize_t n = read(c, req, sizeof req - 1);
    if (n <= 0)
        return;
    req[n] = '\0';

    if (sscanf(req, "GET %1023s", path) != 1) {
        reply_error(c, "400 Bad Request");
        return;
    }
    if (strstr(path, "..") != NULL) {                  /* no escape from root */
        reply_error(c, "403 Forbidden");
        return;
    }
    if (strcmp(path, "/") == 0)
        strcpy(path, "/index.html");
    snprintf(full, sizeof full, "%s%s", root, path);

    struct stat st;
    if (stat(full, &st) == -1 || !S_ISREG(st.st_mode)) {
        reply_error(c, "404 Not Found");               /* PG204 answered "201" by mistake */
        return;
    }
    int fd = open(full, O_RDONLY);
    if (fd == -1) {
        reply_error(c, "403 Forbidden");
        return;
    }
    char head[256];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\n\r\n",
                      mime_type(full), (long long)st.st_size);
    if (write_all(c, head, (size_t)hn) == -1) {
        close(fd);
        return;
    }
    if (st.st_size > 0) {                              /* mmap of 0 bytes is an error */
        void *data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data != MAP_FAILED) {
            (void)write_all(c, data, (size_t)st.st_size);   /* loops over partial writes */
            munmap(data, (size_t)st.st_size);
        }
    }
    close(fd);
}

/* ---------- thread per connection ---------- */
static void *connection_thread(void *arg)
{
    int c = (int)(intptr_t)arg;
    handle(c);
    close(c);
    return NULL;
}

/* ---------- pool: bounded queue of connected sockets ---------- */
static int queue[QUEUE];
static int q_in = 0, q_out = 0;
static sem_t q_free, q_used;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        sem_wait(&q_used);                       /* wait for a connection */
        pthread_mutex_lock(&q_lock);
        int c = queue[q_out];
        q_out = (q_out + 1) % QUEUE;
        pthread_mutex_unlock(&q_lock);
        sem_post(&q_free);
        handle(c);
        close(c);
    }
    return NULL;
}

/* ---------- event loop: one thread watches every socket ---------- */
static void event_loop(int s)
{
    int ep = epoll_create1(0);
    exit_if(ep == -1, "epoll_create1");
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = s }, ready[64];
    exit_if(epoll_ctl(ep, EPOLL_CTL_ADD, s, &ev) == -1, "epoll_ctl");
    for (;;) {
        int n = epoll_wait(ep, ready, 64, -1);   /* sleeps until some socket is readable */
        if (n == -1 && errno == EINTR)
            continue;
        exit_if(n == -1, "epoll_wait");
        for (int i = 0; i < n; i++) {
            int fd = ready[i].data.fd;
            if (fd == s) {                       /* a new connection: watch it too */
                int c = accept(s, NULL, NULL);
                if (c == -1)
                    continue;
                ev.data.fd = c;
                exit_if(epoll_ctl(ep, EPOLL_CTL_ADD, c, &ev) == -1, "epoll_ctl");
            } else {                             /* the request has arrived: read won't wait */
                exit_if(epoll_ctl(ep, EPOLL_CTL_DEL, fd, NULL) == -1, "epoll_ctl");
                handle(fd);
                close(fd);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int port = 8080, opt;
    const char *mode = "iterative";

    while ((opt = getopt(argc, argv, "p:m:d:")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'm': mode = optarg; break;
        case 'd': root = optarg; break;
        default:
            fprintf(stderr, "usage: %s [-p port] [-m iterative|fork|thread|pool|epoll] [-d root]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    /* A client that disconnects in the middle of a write must not kill the server. */
    exit_if(signal(SIGPIPE, SIG_IGN) == SIG_ERR, "signal");

    int s = socket(AF_INET, SOCK_STREAM, 0);
    exit_if(s == -1, "socket");
    int yes = 1;
    exit_if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1, "setsockopt");
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    exit_if(bind(s, (struct sockaddr *)&addr, sizeof addr) == -1, "bind");
    exit_if(listen(s, 64) == -1, "listen");
    printf("http server on port %d, mode %s, root %s\n", port, mode, root);
    fflush(stdout);

    int pool = strcmp(mode, "pool") == 0, threads = strcmp(mode, "thread") == 0;
    int forks = strcmp(mode, "fork") == 0;
    if (strcmp(mode, "epoll") == 0)
        event_loop(s);                           /* never returns */
    if (forks)                                   /* finished children are reaped by the kernel */
        exit_if(signal(SIGCHLD, SIG_IGN) == SIG_ERR, "signal");
    if (pool) {
        exit_if(sem_init(&q_free, 0, QUEUE) == -1 || sem_init(&q_used, 0, 0) == -1, "sem_init");
        for (int i = 0; i < WORKERS; i++) {
            pthread_t t;
            exit_if(pthread_create(&t, NULL, worker, NULL) != 0, "pthread_create");
            exit_if(pthread_detach(t) != 0, "pthread_detach");
        }
    }

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c == -1 && errno == EINTR)
            continue;
        exit_if(c == -1, "accept");
        if (pool) {                              /* producer side */
            sem_wait(&q_free);
            pthread_mutex_lock(&q_lock);
            queue[q_in] = c;
            q_in = (q_in + 1) % QUEUE;
            pthread_mutex_unlock(&q_lock);
            sem_post(&q_used);
        } else if (forks) {
            pid_t pid = fork();
            exit_if(pid == -1, "fork");
            if (pid == 0) {
                exit_if(close(s) == -1, "close"); /* the child does not accept */
                handle(c);
                _exit(EXIT_SUCCESS);
            }
            exit_if(close(c) == -1, "close");    /* the parent does not talk to the client */
        } else if (threads) {
            pthread_t t;
            exit_if(pthread_create(&t, NULL, connection_thread, (void *)(intptr_t)c) != 0, "pthread_create");
            exit_if(pthread_detach(t) != 0, "pthread_detach");   /* nobody will join it */
        } else {
            handle(c);
            close(c);
        }
    }
}
