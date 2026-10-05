/*
 * TD3 - exploration 4: the end of a process: zombies and orphans (original slides 80-82 and 88).
 *  1. A child that exits stays as a ZOMBIE (state Z, <defunct>) until its parent calls wait: the
 *     kernel keeps its PID and exit status. ps shows it before and after waitpid.
 *  2. A process whose parent dies is an ORPHAN: the kernel gives it a new parent, init (PID 1) or the
 *     nearest ancestor marked as "subreaper" (on a desktop, usually systemd --user).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

static void pause_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1)
        ;
}

static void ps(pid_t pid)
{
    char cmd[128];
    snprintf(cmd, sizeof cmd, "ps -o pid,ppid,stat,comm -p %d | sed 's/^/    /'", (int)pid);
    fflush(stdout);
    if (system(cmd) == -1)
        perror("system");
}

/* Name of a process, from /proc/PID/comm. */
static void comm_of(pid_t pid, char *name, size_t len)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
    FILE *f = fopen(path, "r");
    if (f == NULL || fgets(name, (int)len, f) == NULL)
        snprintf(name, len, "?");
    name[strcspn(name, "\n")] = '\0';
    if (f != NULL)
        fclose(f);
}

int main(void)
{
    /* 1. Zombie */
    pid_t child = fork();
    exit_if(child == -1, "fork");
    if (child == 0)
        _exit(7);
    pause_ms(200);
    printf("1) the child %d has exited, but nobody has called wait yet:\n", (int)child);
    ps(child);
    int status;
    exit_if(waitpid(child, &status, 0) == -1, "waitpid");
    printf("   waitpid -> WIFEXITED = %d, WEXITSTATUS = %d; now ps finds nothing:\n", WIFEXITED(status), WEXITSTATUS(status));
    ps(child);

    /* 2. Orphan: the middle process exits, the grandchild is adopted */
    printf("2) orphan: my PID is %d\n", (int)getpid());
    fflush(stdout);
    pid_t middle = fork();
    exit_if(middle == -1, "fork");
    if (middle == 0) {
        pid_t grandchild = fork();
        exit_if(grandchild == -1, "fork");
        if (grandchild == 0) {
            pid_t before = getppid();
            pause_ms(200);                         /* meanwhile its parent exits */
            pid_t after = getppid();
            char name[64];
            comm_of(after, name, sizeof name);
            printf("   grandchild %d: parent was %d, now it is %d (%s)\n", (int)getpid(), (int)before, (int)after, name);
            exit(EXIT_SUCCESS);
        }
        exit(EXIT_SUCCESS);                        /* the grandchild becomes an orphan */
    }
    exit_if(waitpid(middle, NULL, 0) == -1, "waitpid");
    pause_ms(400);                                 /* let the grandchild report */
    return EXIT_SUCCESS;
}
