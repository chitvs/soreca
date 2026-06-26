/*
 * This is part of the fourth session.
 *
 * Goals:
 * - learn to perform input and output operations using descriptors (fd) in UNIX
 * - read/write to files
 * - implement IPC (Inter-Process Communication) using anonymous pipes
 * - implement IPC between unrelated processes using named pipes (FIFOs)
 * - send/receive messages on sockets (future labs)
 *
 * Exercise 1 - Copy a file in C
 * 
 * The goal is to perform a copy of a source file (S) into a destination
 * file (D) through a sequence of reads from S and writes to D, processing
 * data in blocks of B bytes at a time.
 * 
 * How is the execution coordinated?
 * 
 * The application runs as a single process that continuously loops:
 * 1. Reads up to B bytes from the source file descriptor.
 * 2. Writes the exact number of read bytes to the destination file descriptor.
 * 
 * Critical aspects handled in this implementation:
 * - EOF (end of file): recognized when read() returns 0.
 * - EINTR (interrupts): if read() or write() are interrupted by a signal 
 * before processing any byte, they return -1 and set errno to EINTR. 
 * The operation must be retried.
 * - Partial I/O: read() or write() might process fewer bytes than requested.
 * The loop tracks the remaining bytes and adjusts buffer offsets accordingly.
 */

#include <errno.h>
#include <fcntl.h> // macros for open (e.g., O_RDONLY, O_WRONLY)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// macros for error handling
#include "common.h"

#define DEFAULT_BLOCK_SIZE 128

/*
 * Perform copy between descriptors
 *
 * Copies data from a source file descriptor to a destination file descriptor
 * in chunks of 'block_size' bytes.
 */
static inline void performCopyBetweenDescriptors(int src_fd, int dest_fd, int block_size){
    char *buf = malloc(block_size);

    while (1) {
        int read_bytes = 0;          // index for writing into the buffer
        int bytes_left = block_size; // number of bytes to (possibly) read

        /* 
         * Reading loop 
         *
         * Ensures the buffer is filled up to 'block_size', handling partial reads.
         */
        while (bytes_left > 0) {

            int ret = read(src_fd, buf + read_bytes, bytes_left);

            /* EOF reached */
            if (ret == 0) break;

            if (ret == -1) {
                /*
                 * Interrupt handling
                 *
                 * If the read is interrupted by a signal before reading any data,
                 * it returns -1 and errno is set to EINTR. In this case, 
                 * the operation must be retried.
                 */
                if (errno == EINTR)
                    continue;

                handle_error("read error");
            }

            bytes_left -= ret;
            read_bytes += ret;
        }

        // no more bytes left to write!
        if (read_bytes == 0)
            break;

        int written_bytes = 0;   // index for reading from the buffer
        bytes_left = read_bytes; // number of bytes to write

        /* 
         * Writing loop 
         *
         * Ensures all 'read_bytes' are actually written, handling partial writes.
         */
        while (bytes_left > 0) {

            int ret = write(dest_fd, buf + written_bytes, bytes_left);

            if (ret == -1) {
                /*
                 * Interrupt handling
                 *
                 * If the write is interrupted by a signal before writing any data,
                 * it returns -1 and errno is set to EINTR. The operation 
                 * must be retried.
                 */
                if (errno == EINTR) continue;

                handle_error("write error");
            }

            bytes_left -= ret;
            written_bytes += ret;
        }
    }

    free(buf);
}

int main(int argc, char *argv[]) {
    int block_size, src_fd, dest_fd;

    /* Argument parsing */
    if (argc == 4) block_size = atoi(argv[3]);
    else block_size = DEFAULT_BLOCK_SIZE;

    if (argc < 3 || argc > 4) handle_error("Syntax: <source_file> <dest_file> [<block_size>]\n");

    if (block_size <= 0) handle_error("Blocksize must be positive");

    // create descriptors for source and destination files
    src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) handle_error("Could not open source file");

    /* 
     * Destination file creation
     *
     * For simplicity, rw-r--r-- permissions are used for the destination file.
     * O_EXCL ensures safety against accidental overwrites. If the file already 
     * exists (EEXIST), a warning is printed and the file is overwritten.
     */
    dest_fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (dest_fd < 0) {
        if (errno == EEXIST) {
            fprintf(stderr, "WARNING: file %s already exists, I will overwrite it!\n", argv[2]);
            dest_fd = open(argv[2], O_WRONLY | O_CREAT, 0644);
        }
        else
            handle_error("Could not create destination file");
    }

    // use a helper method to actually perform the copy
    performCopyBetweenDescriptors(src_fd, dest_fd, block_size);

    /* 
     * Cleanup 
     * Close the file descriptors to release OS resources.
     */
    int ret = close(src_fd);
    if (ret < 0) handle_error("Could not close source file");
    ret = close(dest_fd);
    if (ret < 0) handle_error("Could not close destination file");
    exit(EXIT_SUCCESS);
}
