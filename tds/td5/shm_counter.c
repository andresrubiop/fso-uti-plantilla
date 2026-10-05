/*
 * TD5 - exercise 5: a System V shared memory segment that survives the processes.
 *   ./shm_counter            increments the first int of the segment and prints it
 *   ./shm_counter --remove   destroys the segment (IPC_RMID)
 * Run it several times: 1, 2, 3... The segment lives in the kernel until it is removed or
 * the machine reboots (list them with `ipcs -m`, remove with `ipcrm`).
 * The key comes from ftok(file, id): same file + same id = same key on this machine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include "utils.h"

#define KEYFILE "/etc/passwd"     /* any file that exists on every Linux machine */
#define KEYID   'F'
#define SIZE    1024

int main(int argc, char *argv[])
{
    key_t key = ftok(KEYFILE, KEYID);
    exit_if(key == -1, "ftok");
    int id = shmget(key, SIZE, IPC_CREAT | 0644);
    exit_if(id == -1, "shmget");

    if (argc > 1 && strcmp(argv[1], "--remove") == 0) {
        exit_if(shmctl(id, IPC_RMID, NULL) == -1, "shmctl IPC_RMID");
        printf("segment %d removed\n", id);
        return EXIT_SUCCESS;
    }

    int *counter = shmat(id, NULL, 0);
    exit_if(counter == (void *)-1, "shmat");
    counter[0]++;                                /* a fresh segment is zero-filled */
    printf("segment %d (key 0x%08x): counter = %d\n", id, (unsigned)key, counter[0]);
    exit_if(shmdt(counter) == -1, "shmdt");
    return EXIT_SUCCESS;
}
