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
 * Exercise 3 - EchoProcess on FIFO
 * 
 * Client (client.c)
 * 
 * The Client process connects to the existing FIFOs created by the Echo process.
 * It reads user input from stdin, sends it through 'client_fifo', and waits
 * for the echoed response on 'echo_fifo'.
 * 
 * Server (echo.c)
 * 
 * The Echo process acts as a server. It creates two FIFOs using mkfifo():
 * - echo_fifo: used to send messages to the Client (O_WRONLY).
 * - client_fifo: used to receive messages from the Client (O_RDONLY).
 * 
 * It continuously reads incoming messages from the Client and echoes them back,
 * until the specific QUIT_COMMAND is received.
 * 
 * I/O Helpers (rw.c)
 *
 * Provides safe read and write operations on file descriptors, specifically
 * tailored for FIFO communication where message lengths might be unknown
 * and system calls can be interrupted.
 */

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include "common.h"

/*
 * Read one by one
 *
 * Reads data from a FIFO as from a regular file descriptor. Since the total 
 * length of the incoming message is unknown, it reads exactly one byte at a time.
 * 
 * - Leaves the cycle when the 'separator' character (e.g., '\n') is encountered.
 * - Repeats the read() operation if interrupted by a signal (EINTR) before reading.
 * - If read() returns 0 bytes, it means the other process has closed the FIFO 
 * unexpectedly. This is handled as a critical error.
 */
int readOneByOne(int fd, char* buf, char separator) {
    int ret;
    int bytes_read = 0;
    
    do {
        ret = read(fd, buf + bytes_read, 1);
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot read from FIFO");
        
        /* Unexpected EOF handling */
        if (ret == 0) {
            printf("%s\n", buf);
            fflush(stdout);
            handle_error_en(bytes_read, "Process has closed the FIFO unexpectedly! Exiting...");
        }
    } while(buf[bytes_read++] != separator);
    
    printf("Read %d bytes\n", bytes_read);
    fflush(stdout);
    
    return bytes_read;
}

/*
 * Write message
 *
 * Writes data on the FIFO as on a regular file descriptor.
 * Uses a while loop to ensure that all bytes requested are actually written,
 * handling partial writes and system interruptions (EINTR).
 */
void writeMsg(int fd, char* buf, int size) {
    int ret;
    int bytes_sent = 0;
    
    while (bytes_sent < size) {
        ret = write(fd, buf + bytes_sent, size - bytes_sent);
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to FIFO");
        
        bytes_sent += ret;
    }
    
    printf("Sent %d bytes\n", bytes_sent);
    fflush(stdout);
}
