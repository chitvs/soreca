/*
 * This is part of the sixth session.
 *
 * Goals:
 * - learn to perform input and output operations using descriptors (fd) in UNIX
 * - implement IPC between unrelated processes using named pipes (FIFOs)
 * - implement communication through client-server architecture over TCP protocol
 *
 * Exercise 2 - TimeServer with sockets
 *
 * Client (client.c)
 *
 * The client connects to the server via a TCP socket. Once the connection
 * is established, it sends a specific command (SERVER_COMMAND) to request 
 * the current time. It then waits for the server's response, prints it, 
 * and closes the connection.
 *
 * Server (server.c)
 *
 * The server listens for incoming TCP connections on a specific port.
 * When a client connects, the server reads the incoming command. If the 
 * command matches the expected SERVER_COMMAND, it replies with the current 
 * date and time. Otherwise, it replies with an "INVALID REQUEST" message.
 * After sending the reply, the server closes the connection with that client 
 * and waits for the next one.
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

    /* Variables for handling a socket */
    int socket_desc;
    struct sockaddr_in server_addr = {0}; /* Fields are required to be filled with 0 */

    /* Create a socket */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0) handle_error("Could not create socket");

    /* Set up parameters for the connection */
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); /* Network byte order conversion */

    /* Initiate a connection on the socket */
    ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    if (ret < 0) handle_error("Could not create connection");

    if (DEBUG) fprintf(stderr, "Connection established!\n");

    /* Send command to server */
    char* command = SERVER_COMMAND;
    size_t command_len = strlen(command);

    int bytes_sent = 0;
    while (bytes_sent < command_len) {
        /* send() with flags = 0 is equivalent to write() on a descriptor */
        ret = send(socket_desc, command + bytes_sent, command_len - bytes_sent, 0);
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to the socket");
        
        bytes_sent += ret;
    }

    if (DEBUG) fprintf(stderr, "Message of %d bytes sent\n", bytes_sent);

    /* Read message from the server */
    char recv_buf[256];
    size_t recv_buf_len = sizeof(recv_buf) - 1; /* Leave space for string terminator */
    int recv_bytes = 0;
    
    while (1) {
        /* recv() with flags = 0 is equivalent to read() on a descriptor */
        ret = recv(socket_desc, recv_buf + recv_bytes, recv_buf_len - recv_bytes, 0);
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot read from the socket");
        
        /* 0 is returned when the peer (server) closes the connection */
        if (ret == 0) break;
        
        recv_bytes += ret;
    }
    
    if (DEBUG) fprintf(stderr, "Message of %d bytes received\n", recv_bytes);

    /* Add string terminator manually safely */
    recv_buf[recv_bytes] = '\0';

    /* Close the socket */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close socket");

    printf("Answer from server: %s", recv_buf);

    if (DEBUG) fprintf(stderr, "Exiting...\n");

    exit(EXIT_SUCCESS);
}
