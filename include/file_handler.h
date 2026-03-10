#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <sys/types.h>

// Function Prototypes

/**
 * Applies a record-level lock.
 * fd: File Descriptor
 * type: F_WRLCK (Write/Exclusive) or F_RDLCK (Read/Shared)
 * offset: Byte offset where the record begins
 * size: Size of the record in bytes
 */
int lock_record(int fd, int type, off_t offset, size_t size);

/**
 * Releases a record-level lock.
 * fd: File Descriptor
 * offset: Byte offset where the record begins
 * size: Size of the record in bytes
 */
int unlock_record(int fd, off_t offset, size_t size);

#endif