/*
 * TD2 - exercise 1: type of the standard input with fstat(2).
 *   ./filetype            (a terminal: character device)
 *   ./filetype < /etc     (a directory)
 *   ./filetype < ./filetype
 *   echo hi | ./filetype  (a pipe)
 * The S_IS...() macros test the st_mode field (see inode(7)).
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

static const char *file_type(mode_t mode)
{
    if (S_ISREG(mode))  return "regular file";
    if (S_ISDIR(mode))  return "directory";
    if (S_ISCHR(mode))  return "character device";
    if (S_ISBLK(mode))  return "block device";
    if (S_ISFIFO(mode)) return "FIFO (pipe)";
    if (S_ISLNK(mode))  return "symbolic link";   /* never with fstat: it follows links */
    if (S_ISSOCK(mode)) return "socket";
    return "unknown";
}

int main(void)
{
    struct stat st;

    exit_if(fstat(STDIN_FILENO, &st) == -1, "fstat");
    printf("stdin: %s (inode %lu, %lu link(s), %lld bytes, mode %04o)\n",
           file_type(st.st_mode), (unsigned long)st.st_ino, (unsigned long)st.st_nlink,
           (long long)st.st_size, (unsigned)(st.st_mode & 07777));
    return EXIT_SUCCESS;
}
