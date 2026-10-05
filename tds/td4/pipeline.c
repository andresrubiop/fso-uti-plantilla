/*
 * TD4 - exercise 4: run a fixed pipeline, like the shell does for  ls -l | tr -d '[:blank:]'
 * Works for any number of commands. Returns the exit status of the LAST command, or
 * EXIT_FAILURE (with a message) if it was killed by a signal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

static char *ls[] = { "ls", "-l", NULL };
static char *tr[] = { "tr", "-d", "[:blank:]", NULL };
static char **commands[] = { ls, tr };
static const int ncommands = sizeof commands / sizeof commands[0];

int main(void)
{
    int in = STDIN_FILENO;      /* where the next command reads from */
    pid_t last = -1;

    for (int i = 0; i < ncommands; i++) {
        int fds[2] = { -1, -1 };
        if (i < ncommands - 1)                        /* every command but the last writes to a pipe */
            exit_if(pipe(fds) == -1, "pipe");

        pid_t pid = fork();
        exit_if(pid == -1, "fork");
        if (pid == 0) {
            if (in != STDIN_FILENO) {                 /* not the first: read from previous pipe */
                exit_if(dup2(in, STDIN_FILENO) == -1, "dup2 stdin");
                exit_if(close(in) == -1, "close");
            }
            if (fds[1] != -1) {                       /* not the last: write to the new pipe */
                exit_if(dup2(fds[1], STDOUT_FILENO) == -1, "dup2 stdout");
                exit_if(close(fds[0]) == -1 || close(fds[1]) == -1, "close");
            }
            execvp(commands[i][0], commands[i]);
            perror(commands[i][0]);
            _exit(127);
        }

        /* parent: close what the child inherited, keep only the read end for the next one */
        if (in != STDIN_FILENO)
            exit_if(close(in) == -1, "close");
        if (fds[1] != -1) {
            exit_if(close(fds[1]) == -1, "close");
            in = fds[0];
        }
        last = pid;
    }

    /* Wait for every child (no zombies), remember the status of the last one. */
    int status, last_status = 0;
    pid_t pid;
    while ((pid = wait(&status)) > 0)
        if (pid == last)
            last_status = status;

    if (WIFEXITED(last_status))
        return WEXITSTATUS(last_status);
    if (WIFSIGNALED(last_status))
        fprintf(stderr, "pipeline: last command killed by %s\n", signame(WTERMSIG(last_status)));
    return EXIT_FAILURE;
}
