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

/*
 * Global cleanup
 *
 * Closes local descriptors and unlinks the FIFOs from the operating system.
 */
static void cleanFIFOs(int echo_fifo, int client_fifo) {
    int ret = close(echo_fifo);
    if(ret) handle_error("Cannot close Echo FIFO");
    
    ret = close(client_fifo);
    if(ret) handle_error("Cannot close Client FIFO");

    ret = unlink(ECHO_FIFO_NAME);
    if(ret) handle_error("Cannot unlink Echo FIFO");
    
    ret = unlink(CLNT_FIFO_NAME);
    if(ret) handle_error("Cannot unlink Client FIFO");
}

int main(int argc, char* argv[]) {
    int ret;
    int echo_fifo, client_fifo;
    char buf[1024];

    char* quit_command = QUIT_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    /*
     * Preventive unlink and FIFO creation
     */
    unlink(ECHO_FIFO_NAME);
    unlink(CLNT_FIFO_NAME);
    
    ret = mkfifo(ECHO_FIFO_NAME, 0666);
    if(ret) handle_error("Cannot create Echo FIFO");
    
    ret = mkfifo(CLNT_FIFO_NAME, 0666);
    if(ret) handle_error("Cannot create Client FIFO");

    /*
     * Open the two FIFOs
     *
     * The Echo program sends data through 'echo_fifo' (O_WRONLY) 
     * and receives through 'client_fifo' (O_RDONLY).
     *
     * The two FIFOs must be opened in the exact same sequential order 
     * in both the Echo and the Client programs to avoid deadlocks. 
     *
     * echo first, client second.
     */
    echo_fifo = open(ECHO_FIFO_NAME, O_WRONLY);
    if(echo_fifo == -1) handle_error("Cannot open Echo FIFO for writing");
    
    client_fifo = open(CLNT_FIFO_NAME, O_RDONLY);
    if(client_fifo == -1) handle_error("Cannot open Client FIFO for reading");

    /* Send welcome message */
    sprintf(buf, "Hi! I'm an Echo process based on FIFOs. I will send you back through a FIFO whatever"
            " you send me through the other FIFO, and I will stop and exit when you send me %s.\n", quit_command);

    writeMsg(echo_fifo, buf, strlen(buf));

    /* Main server loop */
    while (1) {
        memset(buf, 0, 1024);
        
        /* Read message through the client FIFO */
        int bytes_read = readOneByOne(client_fifo, buf, '\n');

        if (DEBUG) {
            buf[bytes_read] = '\0';
            printf("Message received: %s", buf);
        }

        /* Check for the quit command */
        if (bytes_read == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) break;

        /* Send the message back through the Echo FIFO */
        writeMsg(echo_fifo, buf, bytes_read);
    }

    /* Cleanup phase */
    cleanFIFOs(echo_fifo, client_fifo);
    exit(EXIT_SUCCESS);
}
