#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>  // htons()
#include <netinet/in.h> // struct sockaddr_in
#include <sys/socket.h>

#include "common.h"

// Method for processing incoming requests. The method takes as argument
// the socket descriptor for the incoming connection.
void* connection_handler(int socket_desc) {
    int ret, recv_bytes, bytes_sent;

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;
    memset(buf,0,buf_len);

    char* quit_command = SERVER_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    // send welcome message
    sprintf(buf, "Hi! I'm an echo server. I will send you back whatever"
            " you send me. I will stop if you send me %s", quit_command);
    msg_len = strlen(buf);
    
    /*
     * Send the welcome message to the client.
     * The while loop handles partial transmissions to ensure all 'msg_len' bytes 
     * are reliably sent using the socket descriptor.
     */
    bytes_sent = 0;
    while ( bytes_sent < msg_len) {
        ret = send(socket_desc, buf + bytes_sent, msg_len - bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to the socket");
        bytes_sent += ret;
    }

    if (DEBUG) fprintf(stderr, "Welcome message <<%s>> has been sent\n",buf);

    // echo loop
    while (1) {
        /*
         * Receive the client's command or message.
         * The loop reads data one byte at a time until a newline ('\n') is found 
         * or the connection is closed (ret == 0). This blocks until data arrives.
         * The received count is stored in 'recv_bytes'.
         */
        recv_bytes = 0;
        do {
            ret = recv(socket_desc, buf + recv_bytes, 1, 0);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot read from the socket");
            if (ret == 0) break;
        } while ( buf[recv_bytes++] != '\n' );

        if (DEBUG) fprintf(stderr, "Received command of %d bytes...\n",recv_bytes);

        /*
         * Check if the received command matches the quit string (SERVER_COMMAND).
         * If the message length and byte-by-byte comparison match, the server
         * breaks the echo loop to shut down the connection gracefully.
         */
        if (recv_bytes == quit_command_len && !memcmp(buf, quit_command, quit_command_len)){

            if (DEBUG) fprintf(stderr, "Received QUIT command...\n");
             break;
         }

        // ...or I have to send the message back
        /*
         * Echo the received message back to the client.
         * The loop ensures all 'recv_bytes' are sent, handling potential partial 
         * writes until the full message is transmitted.
         */
        bytes_sent=0;
        while ( bytes_sent < recv_bytes) {
            ret = send(socket_desc, buf + bytes_sent, recv_bytes - bytes_sent, 0);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent += ret;
        }

        if (DEBUG) fprintf(stderr, "Sent message of %d bytes back...\n", bytes_sent);
    }


    /*
     * Close the socket descriptor for the client connection and release resources.
     */
        ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close socket for incoming connection");

    if (DEBUG) fprintf(stderr, "Socket closed...\n");

    return NULL;
}

int main(int argc, char* argv[]) {
    
    int ret;
    int socket_desc, client_desc;

    // Initialize address structures to zero
    struct sockaddr_in server_addr = {0}, client_addr = {0};

    int sockaddr_len = sizeof(struct sockaddr_in); // Reused for accept()

    /*
     * Create a socket for listening for incoming connections.
     * Uses AF_INET (IPv4) and SOCK_STREAM (TCP).
     */
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
    if (socket_desc < 0)
        handle_error("Could not create socket");

    if (DEBUG) fprintf(stderr, "Socket created...\n");

    /* We enable SO_REUSEADDR to quickly restart our server after a crash:
     * for more details, read about the TIME_WAIT state in the TCP protocol */
    int reuseaddr_opt = 1;
    ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    if (ret < 0)
        handle_error("Cannot set SO_REUSEADDR option");

    /*
     * Configure server address and bind it to the socket.
     * Sets IP to INADDR_ANY (accepting connections on all interfaces) and 
     * converts the port to network byte order using htons().
     * The bind() function associates the address with the listening socket.
     */
    server_addr.sin_addr.s_addr = INADDR_ANY; // we want to accept connections from any interface
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); // don't forget about network byte order!

    ret = bind(socket_desc, (struct sockaddr*) &server_addr, sockaddr_len);
    if (ret < 0)
        handle_error("Cannot bind address to socket");

    if (DEBUG) fprintf(stderr, "Binded address to socket...\n");

    /*
     * Start listening for incoming connections.
     * The MAX_CONN_QUEUE value sets the maximum number of clients that can wait
     * in the connection queue.
     */
    ret = listen(socket_desc, MAX_CONN_QUEUE);
    if (ret < 0)
        handle_error("Cannot listen on socket");

    if (DEBUG) fprintf(stderr, "Socket is listening...\n");

    // loop to handle incoming connections (sequentially)
    while (1) {
        /*
         * Accept an incoming connection.
         * accept() blocks until a client attempts to connect, returning a new
         * socket descriptor ('client_desc') for communication with that client.
         * The client's address is stored in 'client_addr'.
         */
        client_desc = accept(socket_desc, (struct sockaddr*) &client_addr, (socklen_t*) &sockaddr_len);
        if (client_desc < 0)
        handle_error("Cannot open socket for incoming connection");

        if (DEBUG) fprintf(stderr, "Incoming connection accepted...\n");

        connection_handler(client_desc);

        if (DEBUG) fprintf(stderr, "Done!\n");
    }

    exit(EXIT_SUCCESS); // this will never be executed
}