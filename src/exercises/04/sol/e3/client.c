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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> /* mkfifo() */
#include <sys/stat.h>  /* mkfifo() */

#include "common.h"

int readOneByOne(int fd, char* buf, char separator);
void writeMsg(int fd, char* buf, int size);

int main(int argc, char* argv[]) {
    int ret;
    int echo_fifo, client_fifo;
    char buf[1024];

    char* quit_command = QUIT_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    /*
     * Open the two FIFOs
     *
     * FIFOs are opened in the identical order as the server (Echo first, Client second) 
     * to prevent blocking deadlocks.
     * 
     * The Client reads from 'echo_fifo' (O_RDONLY) and writes to 'client_fifo' (O_WRONLY).
     */
    echo_fifo = open(ECHO_FIFO_NAME, O_RDONLY);
    if(echo_fifo == -1) handle_error("Cannot open Echo FIFO for reading");
    
    client_fifo = open(CLNT_FIFO_NAME, O_WRONLY);
    if(client_fifo == -1) handle_error("Cannot open Client FIFO for writing");

    /*
     * Receive and display the welcome message from the Echo process.
     * readOneByOne fetches enough data to fill the buffer up to the newline.
     */
    memset(buf, 0, 1024);
    int bytes_read = readOneByOne(echo_fifo, buf, '\n');

    /* Add a string terminator to safely print the data */
    buf[bytes_read] = '\0';
    printf("%s", buf);

    /* Main client loop */
    while (1) {
        printf("Insert your message: ");

        /* Read a line from stdin (including newline symbol '\n') */
        if (fgets(buf, sizeof(buf), stdin) != (char*)buf){
            handle_error("Error while reading from stdin, exiting...\n");
        }

        int msg_len = strlen(buf);
        
        /* Send message to Echo process via Client FIFO */
        writeMsg(client_fifo, buf, msg_len);

        /*
         * After a quit command is issued, no further data will be received 
         * from the server. The loop must be exited.
         * 
         * Note: msg_len - 1 is used to ignore the trailing '\n' from fgets.
         */
        if (msg_len - 1 == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) break;

        /* Read echoed response from Echo process */
        bytes_read = readOneByOne(echo_fifo, buf, '\n');
        buf[bytes_read] = '\0';
        printf("Server response: %s", buf);
    }

    /*
     * Local cleanup 
     * 
     * The Client only closes its local descriptors. The Echo process 
     * is responsible for unlinking the FIFOs globally.
     */
    ret = close(echo_fifo);
    if(ret) handle_error("Cannot close Echo FIFO");
    
    ret = close(client_fifo);
    if(ret) handle_error("Cannot close Client FIFO");

    exit(EXIT_SUCCESS);
}
