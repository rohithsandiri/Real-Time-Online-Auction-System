#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include "common.h"

// Generic Record Lock Function
// fd: File Descriptor
// type: F_WRLCK (Write/Exclusive) or F_RDLCK (Read/Shared)
// offset: Where the record starts (id * sizeof(struct))
// size: Size of the record (sizeof(struct))
int lock_record(int fd, int type, off_t offset, size_t size) {
    struct flock lock;
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = offset;
    lock.l_len = size;
    lock.l_pid = getpid();

    // F_SETLKW = Set Lock Wait (Blocking lock)
    // It waits until the lock is available
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        perror("fcntl error");
        return -1;
    }
    return 0;
}

int unlock_record(int fd, off_t offset, size_t size) {
    struct flock lock;
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = offset;
    lock.l_len = size;
    lock.l_pid = getpid();

    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        perror("fcntl unlock error");
        return -1;
    }
    return 0;
}