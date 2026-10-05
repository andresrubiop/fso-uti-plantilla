/*
 * TD3 - exercise 4: ./launch output command [args...]  behaves like  command args... > output
 *  1. writes the command line into `output`
 *  2. forks; the child redirects its stdout to `output` with dup2 and runs the command (execvp)
 *  3. the parent waits and reports how the command ended (exit status or signal)
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s output command [args...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* O_TRUNC: like "> output", previous content is discarded. 0644 = rw-r--r-- */
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    exit_if(fd == -1, argv[1]);

    for (int i = 2; i < argc; i++) {
        exit_if(write_all(fd, argv[i], strlen(argv[i])) == -1, "write");
        exit_if(write_all(fd, i < argc - 1 ? " " : "\n", 1) == -1, "write");
    }

    pid_t pid = fork();
    exit_if(pid == -1, "fork");

    if (pid == 0) {                                   /* child */
        exit_if(dup2(fd, STDOUT_FILENO) == -1, "dup2");
        exit_if(close(fd) == -1, "close");            /* 1 now points to the file */
        execvp(argv[2], argv + 2);                    /* only returns on error */
        perror(argv[2]);
        _exit(127);                                   /* like the shell: command not found */
    }

    exit_if(close(fd) == -1, "close");                /* the parent does not need it */

    int status;
    exit_if(waitpid(pid, &status, 0) == -1, "waitpid");
    if (WIFEXITED(status)) {
        fprintf(stderr, "launch: %s exited with status %d\n", argv[2], WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "launch: %s killed by signal %d (%s)%s\n", argv[2], WTERMSIG(status),
                signame(WTERMSIG(status)), WCOREDUMP(status) ? ", core dumped" : "");
        return 128 + WTERMSIG(status);                /* shell convention */
    }
    return EXIT_FAILURE;
}
