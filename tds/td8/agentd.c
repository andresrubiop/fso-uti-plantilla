/*
 * TD8 - agentd: a miniature "AI-native OS" service. It gathers the whole course in one program:
 *   - a UNIX domain socket (TD7) with 0600 permissions: only its owner may talk to the agent
 *   - a pool of worker threads fed by a bounded queue (TD6)
 *   - the model weights mapped once with mmap and shared by every thread (TD5)
 *   - each tool runs in a NEW process, fork + exec of agent_exec (TD3), whose output comes back
 *     through a pipe (TD4) and whose resources are limited (setrlimit, alarm: TD5)
 *   - the identity of the caller comes from the kernel (SO_PEERCRED), and every decision is
 *     appended to an audit log (O_APPEND, TD1/TD2)
 *
 * The flow of a request - "the model PROPOSES, the operating system DISPOSES":
 *   request -> micro-LLM -> tool call (text) -> strict parser -> policy (allow / ask / deny)
 *           -> [user consent] -> sandboxed process (Landlock) -> result -> audit log
 *
 *   ./agentd [-s socket] [-w workspace] [-m weights] [-t threads] [--no-policy]
 *   --no-policy turns off the path checks of the policy: use it to see that the Landlock sandbox
 *   of agent_exec still stops what the policy would have stopped (defence in depth).
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "tinyllm.h"
#include "utils.h"

#define QUEUE     16
#define MAX_REQ   200
#define MAX_ARGS  2
#define MAX_OUT   (16 * 1024)

static tl_model model;
static char workspace[PATH_MAX];
static char exec_path[PATH_MAX + 16];
static const char *audit_path = "audit.log";
static int policy_on = 1;
static int audit_fd = -1;

/* ------------------------------------------------------------------------------ tool catalogue */
typedef struct {
    const char *name;
    int nargs;      /* number of string arguments */
    int path_arg;   /* index of the argument that is a path, -1 if none */
    int consent;    /* 1: the user must approve it */
} tool_t;

static const tool_t TOOLS[] = {
    { "list_files", 1, 0, 0 },  { "read_file", 1, 0, 0 },   { "file_info", 1, 0, 0 },
    { "count_lines", 1, 0, 0 }, { "search", 2, 1, 0 },      { "disk_usage", 0, -1, 0 },
    { "processes", 0, -1, 0 },  { "system_info", 0, -1, 0 }, { "make_dir", 1, 0, 0 },
    { "write_note", 2, 0, 0 },  { "delete_file", 1, 0, 1 }, { "none", 0, -1, 0 },
};

typedef struct {
    const tool_t *tool;
    char args[MAX_ARGS][MAX_REQ];
    int nargs;
} call_t;

/* Strict parser: name("arg", "arg"). Anything else is rejected: the model output is UNTRUSTED. */
static int parse_call(const char *s, call_t *c)
{
    char name[32];
    int n = 0;
    while (n < 31 && ((*s >= 'a' && *s <= 'z') || *s == '_'))
        name[n++] = *s++;
    name[n] = '\0';
    if (*s++ != '(')
        return -1;
    c->tool = NULL;
    for (size_t i = 0; i < sizeof TOOLS / sizeof TOOLS[0]; i++)
        if (strcmp(name, TOOLS[i].name) == 0)
            c->tool = &TOOLS[i];
    if (c->tool == NULL)
        return -1;
    c->nargs = 0;
    while (*s == '"') {
        if (c->nargs == MAX_ARGS)
            return -1;
        s++;
        int k = 0;
        while (*s && *s != '"' && k < MAX_REQ - 1)
            c->args[c->nargs][k++] = *s++;
        c->args[c->nargs++][k] = '\0';
        if (*s++ != '"')
            return -1;
        if (*s == ',' && s[1] == ' ')
            s += 2;
        else
            break;
    }
    if (*s != ')' || s[1] != '\0' || c->nargs != c->tool->nargs)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------------------ policy */
typedef enum { ALLOW, ASK, DENY } decision_t;
static const char *DECISION[] = { "allow", "ask", "deny" };

/* Is `rel` a path that stays inside the workspace, even through symbolic links? */
static int inside_workspace(const char *rel, char *why, size_t whysz)
{
    if (rel[0] == '/' || rel[0] == '~') {
        snprintf(why, whysz, "absolute path outside the workspace");
        return 0;
    }
    if (strcmp(rel, "..") == 0 || strncmp(rel, "../", 3) == 0 || strstr(rel, "/../") || strstr(rel, "/..") ) {
        snprintf(why, whysz, "'..' leaves the workspace");
        return 0;
    }
    /* Resolve symbolic links: the file (or, if it does not exist yet, its directory). */
    char full[PATH_MAX * 2], real[PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", workspace, rel);
    if (realpath(full, real) == NULL) {
        char *slash = strrchr(full, '/');
        *slash = '\0';
        if (realpath(full, real) == NULL) {
            snprintf(why, whysz, "directory does not exist");
            return 0;
        }
    }
    size_t n = strlen(workspace);
    if (strncmp(real, workspace, n) != 0 || (real[n] != '\0' && real[n] != '/')) {
        snprintf(why, whysz, "resolves to %s, outside the workspace", real);
        return 0;
    }
    return 1;
}

static decision_t policy(const call_t *c, char *why, size_t whysz)
{
    const tool_t *t = c->tool;
    if (strcmp(t->name, "none") == 0) {
        snprintf(why, whysz, "no tool for this request");
        return DENY;
    }
    if (policy_on && t->path_arg >= 0 && !inside_workspace(c->args[t->path_arg], why, whysz))
        return DENY;
    if (t->consent) {
        snprintf(why, whysz, "destructive action: needs the user's consent");
        return ASK;
    }
    snprintf(why, whysz, policy_on ? "tool in the catalogue, path inside the workspace" : "POLICY DISABLED");
    return ALLOW;
}

/* ------------------------------------------------------------------------------ helpers */
static int send_str(int fd, const char *s) { return write_all(fd, s, strlen(s)) == -1 ? -1 : 0; }

static int send_fmt(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int send_fmt(int fd, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return send_str(fd, buf);
}

static int read_line(int fd, char *buf, size_t size)
{
    size_t n = 0;
    while (n < size - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == -1 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        if (c == '\n')
            break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (int)n;
}

static void audit(uid_t uid, pid_t pid, const char *req, const char *call, const char *decision,
                  const char *outcome)
{
    char line[1024], date[32];
    time_t now = time(NULL);
    strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", localtime(&now));
    int n = snprintf(line, sizeof line, "%s uid=%d pid=%d request=\"%s\" call=%s decision=%s outcome=%s\n",
                     date, (int)uid, (int)pid, req, call, decision, outcome);
    /* One write() on an O_APPEND file: lines of different threads never interleave. */
    if (write_all(audit_fd, line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1)) == -1)
        perror("audit");
}

/* fork + exec agent_exec; its stdout/stderr come back through a pipe and go to the client. */
static const char *run_tool(int client, const call_t *c)
{
    char *argv[8];
    int a = 0;
    argv[a++] = exec_path;
    argv[a++] = workspace;
    argv[a++] = (char *)c->tool->name;
    for (int i = 0; i < c->nargs; i++)
        argv[a++] = (char *)c->args[i];
    argv[a] = NULL;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == -1)
        return "pipe failed";
    pid_t pid = fork();
    if (pid == -1) {
        close(fds[0]);
        close(fds[1]);
        return "fork failed";
    }
    if (pid == 0) {                     /* child: only async-signal-safe calls until exec */
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        execv(exec_path, argv);
        _exit(127);
    }
    close(fds[1]);
    char buf[4096];
    ssize_t n;
    size_t total = 0;
    while ((n = read(fds[0], buf, sizeof buf)) > 0) {
        if (total < MAX_OUT)
            (void)write_all(client, buf, (size_t)n);
        total += (size_t)n;
    }
    close(fds[0]);
    int status;
    if (waitpid(pid, &status, 0) == -1)
        return "waitpid failed";
    if (WIFSIGNALED(status)) {
        send_fmt(client, "[tool killed by %s]\n", signame(WTERMSIG(status)));
        return signame(WTERMSIG(status));
    }
    return WEXITSTATUS(status) == 0 ? "ok" : "error";
}

static void serve(int client)
{
    struct ucred cred = { 0 };
    socklen_t len = sizeof cred;
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1)
        return;

    char req[MAX_REQ], out[256], why[PATH_MAX + 64];
    if (read_line(client, req, sizeof req) == 0)
        return;

    /* 1. The model proposes. */
    int steps = tl_generate(&model, req, out, sizeof out, NULL, NULL);
    if (steps < 0) {
        send_str(client, "error: request too long\n");
        audit(cred.uid, cred.pid, req, "-", "deny", "too long");
        return;
    }
    send_fmt(client, "model: %s\n", out);

    /* 2. The operating system disposes. */
    call_t c;
    if (parse_call(out, &c) == -1) {
        send_str(client, "policy: deny (the model produced an invalid call)\n");
        audit(cred.uid, cred.pid, req, out, "deny", "invalid call");
        return;
    }
    decision_t d = policy(&c, why, sizeof why);
    send_fmt(client, "policy: %s (%s)\n", DECISION[d], why);
    if (d == DENY) {
        audit(cred.uid, cred.pid, req, out, "deny", why);
        return;
    }
    if (d == ASK) {
        char answer[16];
        send_fmt(client, "ask: allow the agent to run %s? [y/N]\n", out);
        read_line(client, answer, sizeof answer);
        if (answer[0] != 'y' && answer[0] != 'Y') {
            send_str(client, "cancelled by the user\n");
            audit(cred.uid, cred.pid, req, out, "ask", "refused by the user");
            return;
        }
    }

    /* 3. Sandboxed execution. */
    const char *outcome = run_tool(client, &c);
    audit(cred.uid, cred.pid, req, out, d == ASK ? "ask:approved" : "allow", outcome);
}

/* ------------------------------------------------------------------------------ thread pool */
static int queue[QUEUE], q_in, q_out;
static sem_t q_free, q_used;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        sem_wait(&q_used);
        pthread_mutex_lock(&q_lock);
        int c = queue[q_out];
        q_out = (q_out + 1) % QUEUE;
        pthread_mutex_unlock(&q_lock);
        sem_post(&q_free);
        serve(c);
        close(c);
    }
    return NULL;
}

static volatile sig_atomic_t stop = 0;
static void on_term(int sig) { (void)sig; stop = 1; }

int main(int argc, char *argv[])
{
    const char *sock_path = "agentd.sock", *weights = NULL, *ws = "workspace";
    int nthreads = 4;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) sock_path = argv[++i];
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) ws = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) weights = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) nthreads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-a") && i + 1 < argc) audit_path = argv[++i];
        else if (!strcmp(argv[i], "--no-policy")) policy_on = 0;
        else {
            fprintf(stderr, "usage: %s [-s socket] [-w workspace] [-m weights] [-t threads] [-a audit.log] [--no-policy]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* agent_exec and the weights live next to this executable. */
    char self[PATH_MAX - 16];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    exit_if(n == -1, "readlink /proc/self/exe");
    self[n] = '\0';
    *strrchr(self, '/') = '\0';
    snprintf(exec_path, sizeof exec_path, "%s/agent_exec", self);
    char wpath[PATH_MAX + 32];
    if (weights == NULL) {
        snprintf(wpath, sizeof wpath, "%s/weights/tinyllm.bin", self);
        weights = wpath;
    }

    exit_if(tl_load(&model, weights) == -1, weights);
    exit_if(realpath(ws, workspace) == NULL, ws);
    audit_fd = open(audit_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    exit_if(audit_fd == -1, audit_path);

    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    exit_if(s == -1, "socket");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    exit_if(strlen(sock_path) >= sizeof addr.sun_path, "socket path too long");
    strcpy(addr.sun_path, sock_path);
    unlink(sock_path);
    exit_if(bind(s, (struct sockaddr *)&addr, sizeof addr) == -1, "bind");
    exit_if(chmod(sock_path, 0600) == -1, "chmod");       /* only the owner can connect */
    exit_if(listen(s, 16) == -1, "listen");

    exit_if(sem_init(&q_free, 0, QUEUE) == -1 || sem_init(&q_used, 0, 0) == -1, "sem_init");
    for (int i = 0; i < nthreads; i++) {
        pthread_t t;
        exit_if(pthread_create(&t, NULL, worker, NULL) != 0, "pthread_create");
        pthread_detach(t);
    }
    struct sigaction sa = { .sa_handler = on_term };      /* no SA_RESTART: accept returns EINTR */
    sigemptyset(&sa.sa_mask);
    exit_if(sigaction(SIGTERM, &sa, NULL) == -1 || sigaction(SIGINT, &sa, NULL) == -1, "sigaction");
    signal(SIGPIPE, SIG_IGN);                              /* a client that leaves early is not fatal */

    printf("agentd: model %ld parameters (%zu bytes mapped), %d threads\n", model.n_params, model.map_size, nthreads);
    printf("agentd: socket %s, workspace %s, policy %s\n", sock_path, workspace, policy_on ? "ON" : "OFF");
    fflush(stdout);

    while (!stop) {
        int c = accept(s, NULL, NULL);
        if (c == -1) {
            if (errno == EINTR)
                continue;
            exit_if(1, "accept");
        }
        sem_wait(&q_free);
        pthread_mutex_lock(&q_lock);
        queue[q_in] = c;
        q_in = (q_in + 1) % QUEUE;
        pthread_mutex_unlock(&q_lock);
        sem_post(&q_used);
    }
    close(s);
    unlink(sock_path);
    tl_unload(&model);
    printf("agentd: stopped\n");
    return EXIT_SUCCESS;
}
