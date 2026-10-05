/*
 * TD8 - agent_exec: runs ONE tool of the agent inside a sandbox built by the kernel.
 *   ./agent_exec WORKSPACE TOOL [ARGS...]            (normally launched by agentd with fork + exec)
 *   ./agent_exec --no-sandbox WORKSPACE TOOL [ARGS...]
 *
 * Before touching anything the process restricts ITSELF, and the restrictions can never be lifted:
 *   1. chdir(WORKSPACE)
 *   2. resource limits: 2 s of CPU, 1 MiB per file written, 64 descriptors, 5 s of real time
 *   3. no_new_privs: no setuid program can give privileges back
 *   4. Landlock (Linux >= 5.13): full access to WORKSPACE, read-only /proc, nothing else on the
 *      file system; no TCP connections (ABI >= 4); no signals or abstract sockets outside the
 *      sandbox (ABI >= 6)
 * The agent daemon already checked the request (policy). The sandbox is the second line of
 * defence: if the policy has a bug, or is disabled (agentd --no-policy), the KERNEL still says no.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

#define MAX_READ 4096

/* ------------------------------------------------------------------------------------ Landlock */
static int ll_create(const struct landlock_ruleset_attr *a, size_t size, __u32 flags)
{
    return (int)syscall(SYS_landlock_create_ruleset, a, size, flags);
}

static int ll_add_path(int ruleset, const char *path, __u64 access)
{
    struct landlock_path_beneath_attr pb = { .allowed_access = access };
    pb.parent_fd = open(path, O_PATH | O_CLOEXEC);
    if (pb.parent_fd == -1)
        return -1;
    int rc = (int)syscall(SYS_landlock_add_rule, ruleset, LANDLOCK_RULE_PATH_BENEATH, &pb, 0);
    close(pb.parent_fd);
    return rc;
}

/* Returns the Landlock ABI version applied, or 0 if the kernel does not support Landlock. */
static int sandbox(const char *workspace)
{
    int abi = ll_create(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 1)
        return 0;

    __u64 fs = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_READ_FILE |
               LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
               LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
               LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
               LANDLOCK_ACCESS_FS_MAKE_SYM;
    if (abi >= 2) fs |= LANDLOCK_ACCESS_FS_REFER;
    if (abi >= 3) fs |= LANDLOCK_ACCESS_FS_TRUNCATE;
    if (abi >= 5) fs |= LANDLOCK_ACCESS_FS_IOCTL_DEV;

    struct landlock_ruleset_attr attr = { .handled_access_fs = fs };
    size_t size = sizeof(__u64);                              /* ABI 1-3: only the fs field */
    if (abi >= 4) {
        attr.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP;
        size = offsetof(struct landlock_ruleset_attr, handled_access_net) + sizeof(__u64);
    }
    if (abi >= 6) {
        attr.scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET | LANDLOCK_SCOPE_SIGNAL;
        size = sizeof attr;
    }
    int rs = ll_create(&attr, size, 0);
    exit_if(rs == -1, "landlock_create_ruleset");

    /* Everything inside the workspace (minus executing programs), read-only /proc. */
    exit_if(ll_add_path(rs, workspace, fs & ~(__u64)LANDLOCK_ACCESS_FS_EXECUTE) == -1, "landlock_add_rule workspace");
    exit_if(ll_add_path(rs, "/proc", LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR) == -1,
            "landlock_add_rule /proc");

    exit_if(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1, "prctl");
    exit_if(syscall(SYS_landlock_restrict_self, rs, 0) == -1, "landlock_restrict_self");
    close(rs);
    return abi;
}

static void limits(void)
{
    struct rlimit cpu = { 2, 3 }, fsize = { 1 << 20, 1 << 20 }, nofile = { 64, 64 };
    exit_if(setrlimit(RLIMIT_CPU, &cpu) == -1, "setrlimit cpu");        /* SIGXCPU after 2 s */
    exit_if(setrlimit(RLIMIT_FSIZE, &fsize) == -1, "setrlimit fsize");  /* SIGXFSZ past 1 MiB */
    exit_if(setrlimit(RLIMIT_NOFILE, &nofile) == -1, "setrlimit nofile");
    alarm(5);                                                          /* SIGALRM: real time */
}

/* ------------------------------------------------------------------------------------ tools */
static int fail(const char *tool, const char *what)
{
    printf("%s: %s: %s\n", tool, what, strerror(errno));
    return EXIT_FAILURE;
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int list_files(const char *dir)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return fail("list_files", dir);
    char *names[512];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 512)
        if (e->d_name[0] != '.')
            names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, by_name);
    printf("%d entries in %s\n", n, dir);
    for (int i = 0; i < n; i++) {
        char path[4096];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (lstat(path, &st) == 0)
            printf("  %c %8lld  %s\n", S_ISDIR(st.st_mode) ? 'd' : S_ISLNK(st.st_mode) ? 'l' : '-',
                   (long long)st.st_size, names[i]);
        free(names[i]);
    }
    return EXIT_SUCCESS;
}

static int read_file(const char *f)
{
    int fd = open(f, O_RDONLY);
    if (fd == -1)
        return fail("read_file", f);
    char buf[MAX_READ + 1];
    ssize_t n = read(fd, buf, MAX_READ);
    close(fd);
    if (n == -1)
        return fail("read_file", f);
    buf[n] = '\0';
    fputs(buf, stdout);
    if (n > 0 && buf[n - 1] != '\n')
        putchar('\n');
    if (n == MAX_READ)
        printf("... (truncated at %d bytes)\n", MAX_READ);
    return EXIT_SUCCESS;
}

static int file_info(const char *f)
{
    struct stat st;
    if (stat(f, &st) == -1)
        return fail("file_info", f);
    char date[64];
    strftime(date, sizeof date, "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
    printf("%s: %s, %lld bytes, mode %04o, modified %s, inode %lu\n", f,
           S_ISDIR(st.st_mode) ? "directory" : S_ISREG(st.st_mode) ? "regular file" : "other",
           (long long)st.st_size, (unsigned)(st.st_mode & 07777), date, (unsigned long)st.st_ino);
    return EXIT_SUCCESS;
}

static int count_lines(const char *f)
{
    int fd = open(f, O_RDONLY);
    if (fd == -1)
        return fail("count_lines", f);
    char buf[4096];
    long lines = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0)
        for (ssize_t i = 0; i < n; i++)
            lines += buf[i] == '\n';
    close(fd);
    printf("%s: %ld lines\n", f, lines);
    return EXIT_SUCCESS;
}

static int search(const char *text, const char *f)
{
    FILE *fp = fopen(f, "r");
    if (fp == NULL)
        return fail("search", f);
    char line[1024];
    int no = 0, found = 0;
    while (fgets(line, sizeof line, fp) != NULL) {
        no++;
        if (strstr(line, text) != NULL && found++ < 20)
            printf("%s:%d: %s%s", f, no, line, strchr(line, '\n') ? "" : "\n");
    }
    fclose(fp);
    printf("%d matching line(s) for \"%s\"\n", found, text);
    return EXIT_SUCCESS;
}

static int disk_usage(void)
{
    struct statvfs v;
    if (statvfs(".", &v) == -1)
        return fail("disk_usage", ".");
    double total = (double)v.f_blocks * v.f_frsize / 1e9, avail = (double)v.f_bavail * v.f_frsize / 1e9;
    printf("file system of the workspace: %.1f GB total, %.1f GB available (%.0f%% used)\n",
           total, avail, 100.0 * (1.0 - (double)v.f_bavail / (double)v.f_blocks));
    return EXIT_SUCCESS;
}

typedef struct { int pid; char comm[32]; unsigned long long ticks; } proc_t;

static int by_ticks(const void *a, const void *b)
{
    const proc_t *x = a, *y = b;
    return (y->ticks > x->ticks) - (y->ticks < x->ticks);
}

static int processes(void)
{
    DIR *d = opendir("/proc");
    if (d == NULL)
        return fail("processes", "/proc");
    static proc_t procs[4096];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < 4096) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9')
            continue;
        char path[300], buf[512];
        snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd == -1)
            continue;
        ssize_t len = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (len <= 0)
            continue;
        buf[len] = '\0';
        char *lp = strchr(buf, '('), *rp = strrchr(buf, ')');   /* comm may contain spaces */
        if (!lp || !rp)
            continue;
        proc_t *p = &procs[n];
        p->pid = atoi(buf);
        snprintf(p->comm, sizeof p->comm, "%.*s", (int)(rp - lp - 1), lp + 1);
        unsigned long long ut = 0, st = 0;
        /* after ") ": state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime */
        if (sscanf(rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) == 2) {
            p->ticks = ut + st;
            n++;
        }
    }
    closedir(d);
    qsort(procs, (size_t)n, sizeof *procs, by_ticks);
    long hz = sysconf(_SC_CLK_TCK);
    printf("%d processes; the 5 that used the most CPU time:\n", n);
    for (int i = 0; i < n && i < 5; i++)
        printf("  %7d  %-20s %10.1f s\n", procs[i].pid, procs[i].comm, (double)procs[i].ticks / (double)hz);
    return EXIT_SUCCESS;
}

static int system_info(void)
{
    struct utsname u;
    struct sysinfo si;
    if (uname(&u) == -1 || sysinfo(&si) == -1)
        return fail("system_info", "uname/sysinfo");
    printf("%s %s (%s), %ld CPUs\n", u.sysname, u.release, u.machine, sysconf(_SC_NPROCESSORS_ONLN));
    printf("memory: %.1f GB total, %.1f GB free; %d processes; up %ld h %ld min\n",
           (double)si.totalram * si.mem_unit / 1e9, (double)si.freeram * si.mem_unit / 1e9, si.procs,
           si.uptime / 3600, si.uptime % 3600 / 60);
    return EXIT_SUCCESS;
}

static int make_dir(const char *d)
{
    if (mkdir(d, 0755) == -1)
        return fail("make_dir", d);
    printf("directory %s created\n", d);
    return EXIT_SUCCESS;
}

static int write_note(const char *f, const char *text)
{
    int fd = open(f, O_WRONLY | O_CREAT | O_APPEND, 0644);   /* append only: never overwrites */
    if (fd == -1)
        return fail("write_note", f);
    int ok = write_all(fd, text, strlen(text)) != -1 && write_all(fd, "\n", 1) != -1;
    close(fd);
    if (!ok)
        return fail("write_note", f);
    printf("note appended to %s\n", f);
    return EXIT_SUCCESS;
}

static int delete_file(const char *f)
{
    if (unlink(f) == -1)
        return fail("delete_file", f);
    printf("%s deleted\n", f);
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    int use_sandbox = 1, a = 1;
    if (argc > 1 && strcmp(argv[1], "--no-sandbox") == 0) {
        use_sandbox = 0;
        a = 2;
    }
    if (argc - a < 2) {
        fprintf(stderr, "usage: %s [--no-sandbox] WORKSPACE TOOL [ARGS...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *ws = argv[a], *tool = argv[a + 1];
    char **args = argv + a + 2;
    int nargs = argc - a - 2;

    exit_if(chdir(ws) == -1, ws);
    limits();
    if (use_sandbox) {
        int abi = sandbox(".");
        fprintf(stderr, abi ? "[sandbox: Landlock ABI %d, workspace only]\n"
                            : "[sandbox: Landlock not available in this kernel]\n", abi);
    } else {
        fprintf(stderr, "[sandbox: DISABLED]\n");
    }

#define NEED(k) do { if (nargs != (k)) { printf("%s: expected %d argument(s)\n", tool, (k)); return EXIT_FAILURE; } } while (0)
    if (!strcmp(tool, "list_files"))  { NEED(1); return list_files(args[0]); }
    if (!strcmp(tool, "read_file"))   { NEED(1); return read_file(args[0]); }
    if (!strcmp(tool, "file_info"))   { NEED(1); return file_info(args[0]); }
    if (!strcmp(tool, "count_lines")) { NEED(1); return count_lines(args[0]); }
    if (!strcmp(tool, "search"))      { NEED(2); return search(args[0], args[1]); }
    if (!strcmp(tool, "disk_usage"))  { NEED(0); return disk_usage(); }
    if (!strcmp(tool, "processes"))   { NEED(0); return processes(); }
    if (!strcmp(tool, "system_info")) { NEED(0); return system_info(); }
    if (!strcmp(tool, "make_dir"))    { NEED(1); return make_dir(args[0]); }
    if (!strcmp(tool, "write_note"))  { NEED(2); return write_note(args[0], args[1]); }
    if (!strcmp(tool, "delete_file")) { NEED(1); return delete_file(args[0]); }
#undef NEED
    printf("%s: unknown tool\n", tool);
    return EXIT_FAILURE;
}
