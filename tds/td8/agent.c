/*
 * TD8 - agent: the client. Sends one request to agentd and prints the conversation.
 *   ./agent [-s socket] [-y | -n] "borra viejo.txt"
 *   -y / -n  answer the consent question automatically (for scripts); otherwise it is asked here
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "utils.h"

int main(int argc, char *argv[])
{
    const char *sock_path = "agentd.sock";
    char autoanswer = 0;
    int opt;
    while ((opt = getopt(argc, argv, "s:yn")) != -1) {
        if (opt == 's') sock_path = optarg;
        else if (opt == 'y' || opt == 'n') autoanswer = (char)opt;
        else {
            fprintf(stderr, "usage: %s [-s socket] [-y|-n] \"request\"\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "usage: %s [-s socket] [-y|-n] \"request\"\n", argv[0]);
        return EXIT_FAILURE;
    }

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    exit_if(s == -1, "socket");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", sock_path);
    exit_if(connect(s, (struct sockaddr *)&addr, sizeof addr) == -1, sock_path);

    exit_if(write_all(s, argv[optind], strlen(argv[optind])) == -1 || write_all(s, "\n", 1) == -1, "write");

    FILE *in = fdopen(s, "r");                  /* read the answer line by line */
    exit_if(in == NULL, "fdopen");
    char line[4096];
    while (fgets(line, sizeof line, in) != NULL) {
        fputs(line, stdout);
        if (strncmp(line, "ask:", 4) != 0)
            continue;
        char answer[16] = "n";
        if (autoanswer) {
            answer[0] = autoanswer;
            printf("> %c (automatic answer)\n", autoanswer);
        } else if (fgets(answer, sizeof answer, stdin) == NULL) {
            strcpy(answer, "n");
        }
        fflush(stdout);
        exit_if(write_all(s, answer, 1) == -1 || write_all(s, "\n", 1) == -1, "write");
    }
    fclose(in);
    return EXIT_SUCCESS;
}
