/*
 * TD2 - exercises 4 and 5.4: a small ls with opendir/readdir/closedir and lstat.
 *   ./myls [-l] [-R] [dir...]      (no directory = ".")
 *   -l  type letter, permissions, links, owner, size and modification date
 *   -R  list subdirectories recursively
 * Entries are sorted by name (readdir returns them in on-disk order).
 */
#include <dirent.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "utils.h"

static int opt_long = 0;
static int opt_recursive = 0;

static char type_letter(mode_t m)
{
    if (S_ISREG(m))  return '-';
    if (S_ISDIR(m))  return 'd';
    if (S_ISLNK(m))  return 'l';
    if (S_ISCHR(m))  return 'c';
    if (S_ISBLK(m))  return 'b';
    if (S_ISFIFO(m)) return 'p';
    if (S_ISSOCK(m)) return 's';
    return '?';
}

static void permissions(mode_t m, char out[10])
{
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        out[i] = (m & (1 << (8 - i))) ? rwx[i] : '-';
    out[9] = '\0';
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void list_dir(const char *dir, int show_header)
{
    DIR *d = opendir(dir);
    if (d == NULL) {                       /* not fatal: report and continue with the others */
        perror(dir);
        return;
    }

    /* Collect the names so that we can sort them. */
    size_t n = 0, cap = 16;
    char **names = malloc(cap * sizeof *names);
    exit_if(names == NULL, "malloc");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (n == cap) {
            cap *= 2;
            names = realloc(names, cap * sizeof *names);
            exit_if(names == NULL, "realloc");
        }
        names[n] = strdup(e->d_name);
        exit_if(names[n] == NULL, "strdup");
        n++;
    }
    exit_if(closedir(d) == -1, "closedir");
    qsort(names, n, sizeof *names, by_name);

    if (show_header)
        printf("%s:\n", dir);
    for (size_t i = 0; i < n; i++) {
        char path[PATH_MAX];
        struct stat st;

        /* snprintf never overflows; the original sprintf into char[255] could. */
        if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path) {
            fprintf(stderr, "%s/%s: path too long\n", dir, names[i]);
            continue;
        }
        if (lstat(path, &st) == -1) {      /* lstat: describes the link itself */
            perror(path);
            continue;
        }
        if (opt_long) {
            char perm[10], date[32];
            struct passwd *pw = getpwuid(st.st_uid);
            struct group *gr = getgrgid(st.st_gid);
            permissions(st.st_mode, perm);
            strftime(date, sizeof date, "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
            printf("%c%s %3lu %-8s %-8s %8lld %s %s", type_letter(st.st_mode), perm,
                   (unsigned long)st.st_nlink, pw ? pw->pw_name : "?", gr ? gr->gr_name : "?",
                   (long long)st.st_size, date, names[i]);
            if (S_ISLNK(st.st_mode)) {
                char target[PATH_MAX];
                ssize_t len = readlink(path, target, sizeof target - 1);
                if (len != -1) {
                    target[len] = '\0';
                    printf(" -> %s", target);
                }
            }
            printf("\n");
        } else {
            printf("%c %s\n", type_letter(st.st_mode), names[i]);
        }
    }

    if (opt_recursive) {
        for (size_t i = 0; i < n; i++) {
            char path[PATH_MAX];
            struct stat st;
            if (!strcmp(names[i], ".") || !strcmp(names[i], ".."))
                continue;
            if (snprintf(path, sizeof path, "%s/%s", dir, names[i]) >= (int)sizeof path)
                continue;
            if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {   /* never follow links */
                printf("\n");
                list_dir(path, 1);
            }
        }
    }
    for (size_t i = 0; i < n; i++)
        free(names[i]);
    free(names);
}

int main(int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv, "lR")) != -1) {
        switch (c) {
        case 'l': opt_long = 1; break;
        case 'R': opt_recursive = 1; break;
        default:
            fprintf(stderr, "usage: %s [-l] [-R] [dir...]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    int ndirs = argc - optind;
    if (ndirs == 0) {
        list_dir(".", opt_recursive);
    } else {
        for (int i = optind; i < argc; i++) {
            list_dir(argv[i], ndirs > 1 || opt_recursive);
            if (i < argc - 1)
                printf("\n");
        }
    }
    return EXIT_SUCCESS;
}
