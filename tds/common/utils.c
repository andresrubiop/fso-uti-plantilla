/*
 * utils.c - implementation of libutils.a (see utils.h).
 *
 * Fixes with respect to the PG204 version:
 *  - the signal-name table used the macOS/BSD numbering (SIGEMT, SIGINFO): on Linux
 *    signame(SIGUSR1) returned a wrong name. It is now built with the system macros;
 *  - fibo() had a different return type in utils.h and utils.c;
 *  - create_waiting_child() ignored its parameter;
 *  - write() results were ignored (partial writes, -Werror with glibc fortify).
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

void exit_if(int condition, const char *prefix)
{
    if (!condition)
        return;
    if (errno != 0)
        perror(prefix);
    else
        fprintf(stderr, "%s\n", prefix);
    exit(EXIT_FAILURE);
}

ssize_t write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    size_t done = 0;

    while (done < len) {
        ssize_t rc = write(fd, p + done, len - done);
        if (rc == -1) {
            if (errno == EINTR)     /* interrupted by a signal before writing: retry */
                continue;
            return -1;
        }
        done += (size_t)rc;
    }
    return (ssize_t)done;
}

#define TRANSFER_SIZE 4096

void transfer(int fd_in, int fd_out)
{
    char buffer[TRANSFER_SIZE];

    for (;;) {
        ssize_t n = read(fd_in, buffer, sizeof buffer);
        if (n == -1 && errno == EINTR)
            continue;
        exit_if(n == -1, "read");
        if (n == 0)                 /* end of file */
            return;
        exit_if(write_all(fd_out, buffer, (size_t)n) == -1, "write");
    }
}

unsigned long long fibo(int n)
{
    if (n < 2)
        return (unsigned long long)n;
    return fibo(n - 1) + fibo(n - 2);
}

int split_args(char *argv[], char *args1[], char *args2[])
{
    char **current = args1;

    while (*argv && strcmp("--", *argv) != 0)
        *current++ = *argv++;
    *current = NULL;
    if (*argv == NULL || current == args1)
        return -1;

    argv++;                         /* skip "--" */
    current = args2;
    while (*argv)
        *current++ = *argv++;
    *current = NULL;
    return current == args2 ? -1 : 0;
}

pid_t create_waiting_child(int seconds)
{
    pid_t pid = fork();

    exit_if(pid == -1, "fork");
    if (pid == 0) {
        sleep((unsigned)seconds);
        _exit(EXIT_SUCCESS);
    }
    return pid;
}

/* Designated initializers: each name lands at the number the running system uses. */
static const char *const signames[] = {
    [SIGHUP] = "SIGHUP",     [SIGINT] = "SIGINT",       [SIGQUIT] = "SIGQUIT",
    [SIGILL] = "SIGILL",     [SIGTRAP] = "SIGTRAP",     [SIGABRT] = "SIGABRT",
    [SIGBUS] = "SIGBUS",     [SIGFPE] = "SIGFPE",       [SIGKILL] = "SIGKILL",
    [SIGUSR1] = "SIGUSR1",   [SIGSEGV] = "SIGSEGV",     [SIGUSR2] = "SIGUSR2",
    [SIGPIPE] = "SIGPIPE",   [SIGALRM] = "SIGALRM",     [SIGTERM] = "SIGTERM",
    [SIGCHLD] = "SIGCHLD",   [SIGCONT] = "SIGCONT",     [SIGSTOP] = "SIGSTOP",
    [SIGTSTP] = "SIGTSTP",   [SIGTTIN] = "SIGTTIN",     [SIGTTOU] = "SIGTTOU",
    [SIGURG] = "SIGURG",     [SIGXCPU] = "SIGXCPU",     [SIGXFSZ] = "SIGXFSZ",
    [SIGVTALRM] = "SIGVTALRM", [SIGPROF] = "SIGPROF",   [SIGWINCH] = "SIGWINCH",
    [SIGIO] = "SIGIO",       [SIGSYS] = "SIGSYS",
};

#define NSIGNAMES ((int)(sizeof signames / sizeof signames[0]))

const char *signame(int sig)
{
    if (sig > 0 && sig < NSIGNAMES && signames[sig] != NULL)
        return signames[sig];
    return "SIG???";
}

#define SIG_BUFFER_SIZE 512

void print_sigset(int fd, const sigset_t *set)
{
    char buffer[SIG_BUFFER_SIZE];
    size_t len = 0;
    int sig;

    /* Only async-signal-safe calls: this function may be used inside a handler. */
    const char *head = "signals:";
    memcpy(buffer, head, strlen(head));
    len = strlen(head);
    for (sig = 1; sig < NSIGNAMES; sig++) {
        if (signames[sig] == NULL || sigismember(set, sig) != 1)
            continue;
        size_t n = strlen(signames[sig]);
        if (len + n + 2 >= sizeof buffer)
            break;
        buffer[len++] = ' ';
        memcpy(buffer + len, signames[sig], n);
        len += n;
    }
    buffer[len++] = '\n';
    if (write_all(fd, buffer, len) == -1)
        return;                     /* nothing sensible to do inside a handler */
}

void print_signals(int fd)
{
    sigset_t set;

    if (sigpending(&set) == 0 && write_all(fd, "pending ", 8) != -1)
        print_sigset(fd, &set);
    if (sigprocmask(SIG_BLOCK, NULL, &set) == 0 && write_all(fd, "blocked ", 8) != -1)
        print_sigset(fd, &set);
}
