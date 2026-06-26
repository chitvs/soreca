/*
 * This is part of the eighth session.
 *
 * Goals:
 * - Understand the C10K Problem and server scalability bottlenecks.
 * - Implement server parallelism using multi-process architecture (fork).
 * - Implement server parallelism using multi-thread architecture (pthread).
 *
 * Exercise 1 - Concurrent echo server
 *
 * Client
 *
 * The client connects to the server via a TCP socket. Once the connection
 * is established, it reads a welcome message from the server. It then enters 
 * a loop where it reads user input from stdin, sends it to the server, and 
 * waits for the echoed response. If the user inputs the QUIT_COMMAND, 
 * the client sends it and exits the loop, terminating the connection.
 *
 * Server
 *
 * The server listens for incoming TCP connections. Through conditional 
 * compilation flags (-DSERVER_SERIAL, -DSERVER_MPROC, -DSERVER_MTHREAD), 
 * the server can be compiled to handle connections in three different ways:
 * 
 * 1. Serially: One client at a time, queueing others.
 * 2. Multi-Process: A new child process is forked for each incoming connection.
 * 3. Multi-Thread: A new detached thread is spawned for each incoming connection.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* htons() and inet_addr() */
#include <netinet/in.h> /* struct sockaddr_in */
#include <sys/socket.h>

#include "common.h"

int main(int argc, char* argv[]) {
    int ret;

    /* Variables for handling the client socket connection */
    int socket_desc;
    struct sockaddr_in server_addr = {0}; /* Some fields are required to be filled with 0 */

    /*
     * Create a TCP socket for contacting the server.
     * - Protocol family: AF_INET (IPv4).
     * - Socket type: SOCK_STREAM (TCP).
     * - Default protocol: 0.
     */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_desc < 0) handle_error("Could not create socket");

    /*
     * Configure the server address structure.
     * 1. Set IP address using inet_addr().
     * 2. Set address family (AF_INET).
     * 3. Set port using htons() for Network Byte Order.
     */
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); /* Network byte order conversion */

    /*
     * Initiate a connection on the socket to the configured server address.
     */
    ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    if(ret) handle_error("Could not create connection");

    if (DEBUG) fprintf(stderr, "Connection established!\n");

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;

    /*
     * Receive and display the initial welcome message from the server.
     *
     * recv() with flags = 0 is equivalent to read() on a descriptor.
     * The call blocks until data becomes available.
     */
    while ( (msg_len = recv(socket_desc, buf, buf_len - 1, 0)) < 0 ) {
        if (errno == EINTR) continue;
        handle_error("Cannot read from socket");
    }
    buf[msg_len] = '\0';
    printf("%s", buf);

    /* Main loop */
    while (1) {
        char* quit_command = SERVER_COMMAND;
        size_t quit_command_len = strlen(quit_command);

        printf("Insert your message: ");

        /*
         * Read a line from stdin
         * 
         * fgets() reads up to sizeof(buf)-1 bytes and on success
         * returns the first argument passed to it. 
         */
        if (fgets(buf, sizeof(buf), stdin) != (char*)buf) {
            fprintf(stderr, "Error while reading from stdin, exiting...\n");
            exit(EXIT_FAILURE);
        }

        msg_len = strlen(buf);
        buf[msg_len-1] = '\n'; /* Place '\n' at the end of the message */

        /*
         * Send the user's message to the server.
         *
         * send() with flags = 0 is equivalent to write() on a descriptor.
         */
        while ((ret = send(socket_desc, buf, msg_len, 0)) < 0) {
            if (errno == EINTR) continue;
            handle_error("Cannot write to socket");
        }

        /*
         * After a quit command, no more data will be received from
         * the server, thus the main loop must be exited. 
         */
        if (msg_len == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) break;

        /*
         * Read the server's response.
         *
         * recv() with flags = 0 is equivalent to read() on a descriptor.
         */
        while ( (msg_len = recv(socket_desc, buf, buf_len, 0)) < 0 ) {
            if (errno == EINTR) continue;
            handle_error("Cannot read from socket");
        }

        printf("Server response: %s\n", buf); /* No need to insert '\0' */
    }

    /*
     * Close socket and release unused resources.
     */
    ret = close(socket_desc);
    if(ret) handle_error("Cannot close socket");

    if (DEBUG) fprintf(stderr, "Exiting...\n");

    exit(EXIT_SUCCESS);
}
