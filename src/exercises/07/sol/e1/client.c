/*
 * This is part of the seventh session.
 *
 * Goals:
 * - understand the difference between network byte order and host byte order
 * - set up a client/server application using TCP and UDP sockets
 * - implement a simple communication protocol based on text messages
 *
 * Exercise 1 - TCP echo server
 *
 * Client
 *
 * The client connects to the server via a TCP socket. Once the connection
 * is established, it receives a welcome message. Then, it enters a loop where
 * it reads user input from stdin, sends it to the server, and waits for the 
 * echoed response. If the user inputs the QUIT_COMMAND, the client sends it 
 * and exits the loop, terminating the connection.
 *
 * Server
 *
 * The server listens for incoming TCP connections on a specific port.
 * When a client connects, the server sends a welcome message. Then, it enters
 * an echo loop: it reads incoming text messages from the client and sends the
 * exact same messages back. If the received message matches the QUIT_COMMAND,
 * the server breaks the loop, closes the connection with that client, and 
 * waits for the next incoming connection.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* For htons() and inet_addr() */
#include <netinet/in.h> /* For struct sockaddr_in */
#include <sys/socket.h> /* For socket API calls */

#include "common.h"

int main(int argc, char* argv[]) {
    int ret, bytes_sent, recv_bytes;

    /* Variables for handling the client socket connection */
    int socket_desc;
    struct sockaddr_in server_addr = {0}; /* Initialize to zero */

    /*
     * Create a TCP socket for contacting the server.
     * - Protocol family: AF_INET (IPv4).
     * - Socket type: SOCK_STREAM (TCP for a reliable, stream-based connection).
     * - Default protocol: 0.
     */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_desc < 0) handle_error("Could not create socket");

    if (DEBUG) fprintf(stderr, "Socket created...\n");

    /*
     * Configure the server address structure and initiate a connection.
     * 1. Set IP address using inet_addr().
     * 2. Set address family (AF_INET).
     * 3. Set port using htons() for Network Byte Order.
     *
     * The connect() call attempts the TCP handshake.
     * Attention to the connect method, it requires the second field to be a 
     * struct sockaddr*, so the struct sockaddr_in must be explicitly cast.
     */
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS); /* Convert IP string to binary format */
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); /* Convert port to network byte order */

    /* Initiate a connection on the socket to the configured server address */
    ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    if (ret < 0) handle_error("Could not create connection");

    if (DEBUG) fprintf(stderr, "Connection established!\n");

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;
    memset(buf, 0, buf_len);

    /*
     * Receive and display the initial welcome message from the server.
     * 
     * - recv() with flags = 0 is equivalent to read() on a descriptor.
     * - A loop is used to deal with partially received messages.
     * - For sockets, a 0 return value is received only when the peer closes 
     * the connection. If there are no bytes to read, recv() blocks.
     * - Error handling checks for EINTR (interruption).
     */
    recv_bytes = 0;
    do {
        /* Read data into the buffer, starting from the position of the last read byte */
        ret = recv(socket_desc, buf + recv_bytes, buf_len - recv_bytes, 0);
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot read from the socket");
        if (ret == 0) break; /* Connection closed by peer */
        recv_bytes += ret;

    } while (buf[recv_bytes - 1] != '\n'); /* Keep reading until the newline character is found */
    printf("%s", buf);

    if (DEBUG) fprintf(stderr, "Received message of %d bytes...\n", recv_bytes);

    /* Main communication loop */
    while (1) {
        char* quit_command = SERVER_COMMAND;
        size_t quit_command_len = strlen(quit_command);

        printf("Insert your message: ");

        /* 
         * Read a line from stdin (the user's message).
         * fgets() reads up to sizeof(buf)-1 bytes and returns a pointer to the buffer on success. 
         */
        if (fgets(buf, sizeof(buf), stdin) != (char*)buf) {
            fprintf(stderr, "Error while reading from stdin, exiting...\n");
            exit(EXIT_FAILURE);
        }

        msg_len = strlen(buf);

        /*
         * Send the user's message to the server.
         *
         * - send() with flags = 0 is equivalent to write() on a descriptor.
         * - A loop is used to deal with partially sent messages.
         * - The size of the message is specifically msg_len, not the total buffer size.
         */
        bytes_sent = 0;
        while (bytes_sent < msg_len) {
            /* Send the remaining part of the message */
            ret = send(socket_desc, buf + bytes_sent, msg_len - bytes_sent, 0);
            if (ret == -1 && errno == EINTR) continue; /* Retry on interruption */
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent += ret; /* Update the count of bytes successfully sent */
        }

        if (DEBUG) fprintf(stderr, "Sent message of %d bytes...\n", bytes_sent);

        /*
         * Check if the sent message was the QUIT command.
         *
         * - Compare the number of bytes sent with the length of the quit command.
         * - Perform a byte-to-byte comparison using memcmp().
         * - Break the cycle when the connection is intended to be closed.
         */
        if (msg_len == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) {

            if (DEBUG) fprintf(stderr, "Sent QUIT command ...\n");
            break; /* Exit the main loop */
        }

        /*
         * Read the server's response.
         *
         * - recv() with flags = 0 is equivalent to read() from a descriptor.
         * - It handles partially sent messages by continuing to read until 
         * the newline character is found.
         * - Checks for ret == 0 to detect if the peer unexpectedly closed the connection.
         */
        recv_bytes = 0;
        do {
            /* Read data into the buffer, starting from the position of the last read byte */
            ret = recv(socket_desc, buf + recv_bytes, buf_len - recv_bytes, 0);
            if (ret == -1 && errno == EINTR) continue; /* Retry on interruption */
            if (ret == -1) handle_error("Cannot read from the socket");
            if (ret == 0) break; /* Connection closed by peer */
            recv_bytes += ret; /* Update the count of bytes successfully received */

        } while (buf[recv_bytes - 1] != '\n'); /* Keep reading until the newline character is found */

        if (DEBUG) fprintf(stderr, "Received answer of %d bytes...\n", recv_bytes);

        printf("Server response: %s\n", buf);
    }

    /*
     * Close socket and release unused resources.
     */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close the socket");

    if (DEBUG) fprintf(stderr, "Socket closed...\n");

    if (DEBUG) fprintf(stderr, "Exiting...\n");

    exit(EXIT_SUCCESS);
}
