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
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>  /* htons() */
#include <netinet/in.h> /* struct sockaddr_in */
#include <sys/socket.h>

#include "common.h"

/*
 * Connection handler 
 *
 * Method for processing incoming requests. The method takes as argument
 * the socket descriptor for the incoming connection.
 */
void* connection_handler(int socket_desc) {
    int ret, recv_bytes, bytes_sent;

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;
    memset(buf, 0, buf_len);

    char* quit_command = SERVER_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    /* Prepare welcome message */
    sprintf(buf, "Hi! I'm an echo server. I will send you back whatever"
            " you send me. I will stop if you send me %s", quit_command);
    msg_len = strlen(buf);
    
    /*
     * Sending welcome message.
     *
     * - send() with flags = 0 is equivalent to write() to a descriptor.
     * - The message has been written in 'buf'.
     * - Deals with partially sent messages by using a while loop.
     * - The size used is 'msg_len', not the entire buffer size.
     */
    bytes_sent = 0;
    while (bytes_sent < msg_len) {
        ret = send(socket_desc, buf + bytes_sent, msg_len - bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to the socket");
        bytes_sent += ret;
    }

    if (DEBUG) fprintf(stderr, "Welcome message <<%s>> has been sent\n", buf);

    /* Echo loop */
    while (1) {
        /*
         * Receive the command.
         *
         * - recv() with flags = 0 is equivalent to read() from a descriptor.
         * - The number of received bytes is tracked in 'recv_bytes'.
         * - For sockets, a 0 return value is obtained only when the peer closes 
         * the connection. If there are no bytes, the blocking call waits.
         * - Deals with unknown message sizes by reading one byte at a time
         * until a newline is encountered.
         */
        recv_bytes = 0;
        do {
            ret = recv(socket_desc, buf + recv_bytes, 1, 0);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot read from the socket");
            if (ret == 0) break;
        } while (buf[recv_bytes++] != '\n');

        if (DEBUG) fprintf(stderr, "Received command of %d bytes...\n", recv_bytes);

        /*
         * Check if the quit_command is received. If so, quit from the echo loop.
         *
         * - Compare the number of bytes received with the length of the quit command.
         * - Perform a byte-to-byte comparison using memcmp().
         * - Exit from the cycle when the quit command is successfully parsed.
         */
        if (recv_bytes == quit_command_len && !memcmp(buf, quit_command, quit_command_len)){

            if (DEBUG) fprintf(stderr, "Received QUIT command...\n");
             break;
         }

        /*
         * Echo the received message back to the client.
         *
         * - send() with flags = 0 is equivalent to write() on a descriptor.
         * - Deals with partially sent messages via a while loop.
         * - The transmission size is specifically 'recv_bytes', NOT the buf size.
         */
        bytes_sent = 0;
        while (bytes_sent < recv_bytes) {
            ret = send(socket_desc, buf + bytes_sent, recv_bytes - bytes_sent, 0);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent += ret;
        }

        if (DEBUG) fprintf(stderr, "Sent message of %d bytes back...\n", bytes_sent);
    }

    /*
     * Close socket and release unused resources.
     */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close socket for incoming connection");

    if (DEBUG) fprintf(stderr, "Socket closed...\n");

    return NULL;
}

int main(int argc, char* argv[]) {
    
    int ret;
    int socket_desc, client_desc;

    /* Initialize address structures to zero */
    struct sockaddr_in server_addr = {0}, client_addr = {0};

    int sockaddr_len = sizeof(struct sockaddr_in); /* Reused for accept() */

    /*
     * Create a socket for listening.
     * 
     * - Protocol family is AF_INET.
     * - Socket type is SOCK_STREAM (TCP).
     */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0)
        handle_error("Could not create socket");

    if (DEBUG) fprintf(stderr, "Socket created...\n");

    /*
     * Enable SO_REUSEADDR to quickly restart our server after a crash:
     * for more details, read about the TIME_WAIT state in the TCP protocol 
     */
    int reuseaddr_opt = 1;
    ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    if (ret < 0)
        handle_error("Cannot set SO_REUSEADDR option");

    /*
     * Set server address and bind it to the socket.
     *
     * - server_addr.sin_addr.s_addr is set to INADDR_ANY to accept connections 
     * from any interface.
     * - server_addr.sin_family is set to AF_INET.
     * - server_addr.sin_port is configured using the htons() method.
     * - The bind method requires the second argument to be struct sockaddr*, 
     * hence the struct sockaddr_in is cast properly.
     */
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); 

    ret = bind(socket_desc, (struct sockaddr*) &server_addr, sockaddr_len);
    if (ret < 0)
        handle_error("Cannot bind address to socket");

    if (DEBUG) fprintf(stderr, "Binded address to socket...\n");

    /*
     * Start listening.
     * 
     * The number of pending connections is set to MAX_CONN_QUEUE.
     */
    ret = listen(socket_desc, MAX_CONN_QUEUE);
    if (ret < 0)
        handle_error("Cannot listen on socket");

    if (DEBUG) fprintf(stderr, "Socket is listening...\n");

    /* Loop to handle incoming connections sequentially */
    while (1) {
        /*
         * Accept an incoming connection.
         *
         * - The descriptor returned by accept() is stored in the client_desc variable.
         * - The address of the client_addr variable is passed, cast to struct sockaddr*.
         * - The size of the client_addr structure is passed via the sockaddr_len 
         * variable, cast to socklen_t*.
         * - The return value of accept() is checked for errors.
         */
        client_desc = accept(socket_desc, (struct sockaddr*) &client_addr, (socklen_t*) &sockaddr_len);
        if (client_desc < 0)
            handle_error("Cannot open socket for incoming connection");

        if (DEBUG) fprintf(stderr, "Incoming connection accepted...\n");

        connection_handler(client_desc);

        if (DEBUG) fprintf(stderr, "Done!\n");
    }

    exit(EXIT_SUCCESS); /* This will never be executed */
}
