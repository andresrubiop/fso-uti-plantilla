/*
 * TD4 - exercise 5: the pipeline comes from the command line, commands separated by "--".
 *   ./pipeline_args ls -l -- grep a -- wc -l      same as      ls -l | grep a | wc -l
 * The "--" entries of argv are replaced by NULL: each command becomes a NULL-terminated
 * slice of argv, which is exactly what execvp needs (no copy, no malloc).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s cmd [args] [-- cmd [args]]...\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Split argv in place. commands[k] points to the first word of command k. */
    char **commands[argc];
    int ncommands = 0;
    commands[ncommands++] = &argv[1];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            argv[i] = NULL;                          /* ends the previous command */
            if (i + 1 >= argc || strcmp(argv[i + 1], "--") == 0 || commands[ncommands - 1][0] == NULL) {
                fprintf(stderr, "%s: empty command\n", argv[0]);
                return EXIT_FAILURE;
            }
            commands[ncommands++] = &argv[i + 1];
        }
    }

    int in = STDIN_FILENO;
    pid_t last = -1;
    for (int i = 0; i < ncommands; i++) {
        int fds[2] = { -1, -1 };
        if (i < ncommands - 1)
            exit_if(pipe(fds) == -1, "pipe");

        pid_t pid = fork();
        exit_if(pid == -1, "fork");
        if (pid == 0) {
            if (in != STDIN_FILENO) {
                exit_if(dup2(in, STDIN_FILENO) == -1, "dup2 stdin");
                exit_if(close(in) == -1, "close");
            }
            if (fds[1] != -1) {
                exit_if(dup2(fds[1], STDOUT_FILENO) == -1, "dup2 stdout");
                exit_if(close(fds[0]) == -1 || close(fds[1]) == -1, "close");
            }
            execvp(commands[i][0], commands[i]);
            perror(commands[i][0]);
            _exit(127);
        }
        if (in != STDIN_FILENO)
            exit_if(close(in) == -1, "close");
        if (fds[1] != -1) {
            exit_if(close(fds[1]) == -1, "close");
            in = fds[0];
        }
        last = pid;
    }

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
