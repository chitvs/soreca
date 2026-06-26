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
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>  /* htons() */
#include <netinet/in.h> /* struct sockaddr_in */
#include <sys/socket.h>

#include "common.h"

void connection_handler(int socket_desc) {
    int ret;
    char* allowed_command = SERVER_COMMAND;
    size_t allowed_command_len = strlen(allowed_command);
    char send_buf[256];

    /* Receive command from client */
    char recv_buf[256];
    size_t recv_buf_len = sizeof(recv_buf);
    int recv_bytes = 0;

    while (recv_bytes < allowed_command_len) {
        ret = recv(socket_desc, recv_buf + recv_bytes, recv_buf_len - recv_bytes, 0);
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot read from the socket");
        
        /* If 0 is returned, the client closed the connection unexpectedly */
        if (ret == 0) break;
        
        recv_bytes += ret;
    }

    if (DEBUG) fprintf(stderr, "Message of %d bytes received\n", recv_bytes);

    /* Parse received command and write reply in send_buf */
    if (recv_bytes == allowed_command_len && !memcmp(recv_buf, allowed_command, allowed_command_len)) {
        time_t curr_time;
        time(&curr_time);
        sprintf(send_buf, "%s", ctime(&curr_time));
    } else {
        sprintf(send_buf, "INVALID REQUEST\n");
    }

    /* Send reply */
    size_t server_message_len = strlen(send_buf);
    int bytes_sent = 0;
    
    while (bytes_sent < server_message_len) {
        ret = send(socket_desc, send_buf + bytes_sent, server_message_len - bytes_sent, 0);
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to the socket");
        
        bytes_sent += ret;
    }
   
    if (DEBUG) fprintf(stderr, "Message of %d bytes sent\n", bytes_sent);

    /* Close socket */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close socket for incoming connection");
}

int main(int argc, char* argv[]) {
    int ret;
    int socket_desc, client_desc;

    /* Fields are required to be filled with 0 */
    struct sockaddr_in server_addr = {0}, client_addr = {0};

    int sockaddr_len = sizeof(struct sockaddr_in); /* Reused for accept() */

    /* Initialize socket for listening */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0) handle_error("Could not create socket");

    server_addr.sin_addr.s_addr = INADDR_ANY; /* Accept connections from any interface */
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); /* Network byte order conversion */

    /*
     * SO_REUSEADDR enablement
     *
     * Allows the server to quickly restart after a crash. 
     * Bypasses the TIME_WAIT state in the TCP protocol.
     */
    int reuseaddr_opt = 1;
    ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    if (ret < 0) handle_error("Cannot set SO_REUSEADDR option");

    /* Bind address to socket */
    ret = bind(socket_desc, (struct sockaddr*) &server_addr, sockaddr_len);
    if (ret < 0) handle_error("Cannot bind address to socket");

    /* Start listening */
    ret = listen(socket_desc, MAX_CONN_QUEUE);
    if (ret < 0) handle_error("Cannot listen on socket");

    /* Loop to handle incoming connections serially */
    while (1) {
        client_desc = accept(socket_desc, (struct sockaddr*) &client_addr, (socklen_t*) &sockaddr_len);
        if (client_desc < 0) handle_error("Cannot open socket for incoming connection");

        if (DEBUG) fprintf(stderr, "Incoming connection accepted...\n");

        connection_handler(client_desc);

        if (DEBUG) fprintf(stderr, "Done!\n");
    }

    exit(EXIT_SUCCESS); /* This will never be executed due to the infinite loop */
}