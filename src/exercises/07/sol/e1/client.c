#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  // For htons() and inet_addr()
#include <netinet/in.h> // For struct sockaddr_in
#include <sys/socket.h> // For socket API calls

#include "common.h"

int main(int argc, char* argv[]) {
    int ret, bytes_sent, recv_bytes;

    // Variables for handling the client socket connection
    int socket_desc;
    struct sockaddr_in server_addr = {0}; // Initialize to zero

    /*
     * Create a TCP socket for contacting the server.
     * - AF_INET: IPv4 protocol family.
     * - SOCK_STREAM: TCP for a reliable, stream-based connection.
     * - 0: Default protocol (TCP).
     */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_desc < 0) handle_error("Could not create socket");

    if (DEBUG) fprintf(stderr, "Socket created...\n");

    /*
     * Configure the server address structure and initiate a connection.
     * 1. Set IP address using inet_addr().
     * 2. Set address family (AF_INET).
     * 3. Set port using htons() for Network Byte Order.
     * The connect() call attempts the TCP handshake.
     * A cast to (struct sockaddr*) is required for the connection call.
     */
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS); // Convert IP string to binary format
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); // Convert port to network byte order

    // Initiate a connection on the socket to the configured server address
    ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    if (ret < 0) handle_error("Could not create connection");

    if (DEBUG) fprintf(stderr, "Connection established!\n");

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;
    memset(buf, 0, buf_len);

    /*
     * Receive and display the initial welcome message from the server.
     * Use a loop with recv() to handle potentially fragmented messages.
     * Reading continues until a newline ('\n') is received or the peer closes.
     * Error handling checks for EINTR (interruption), general error (-1),
     * and peer closing the connection (0).
     */
    recv_bytes = 0;
    do {
        // Read data into the buffer, starting from the position of the last read byte
        ret = recv(socket_desc, buf + recv_bytes, buf_len - recv_bytes, 0);
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot read from the socket");
        if (ret == 0) break; // Connection closed by peer
        recv_bytes += ret;

    } while (buf[recv_bytes - 1] != '\n'); // Keep reading until the newline character is found
    printf("%s", buf);

    if (DEBUG) fprintf(stderr, "Received message of %d bytes...\n", recv_bytes);

    // main communication loop
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
         * Use a loop with send() to handle partial writes and ensure the entire 
         * message (up to msg_len) is transmitted.
         * send() with flags = 0 is equivalent to write().
         */
        bytes_sent = 0;
        while (bytes_sent < msg_len) {
            // Send the remaining part of the message
            ret = send(socket_desc, buf + bytes_sent, msg_len - bytes_sent, 0);
            if (ret == -1 && errno == EINTR) continue; // Retry on interruption
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent += ret; // Update the count of bytes successfully sent
        }

        if (DEBUG) fprintf(stderr, "Sent message of %d bytes...\n", bytes_sent);

        /*
         * Check if the sent message was the QUIT command.
         * * If the message matches SERVER_COMMAND, the server will close the
         * connection, so the client must break the loop. memcmp() is used
         * for byte-by-byte comparison.
         */
        if (msg_len == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) {

            if (DEBUG) fprintf(stderr, "Sent QUIT command ...\n");
            break; // Exit the main loop
        }

        /*
         * Read the server's response.
         * Loop with recv() to read the full response until a newline ('\n') 
         * is received or the server closes the connection (ret == 0).
         */
        recv_bytes = 0;
        do {
            // Read data into the buffer, starting from the position of the last read byte
            ret = recv(socket_desc, buf + recv_bytes, buf_len - recv_bytes, 0);
            if (ret == -1 && errno == EINTR) continue; // Retry on interruption
            if (ret == -1) handle_error("Cannot read from the socket");
            if (ret == 0) break; // Connection closed by peer
            recv_bytes += ret; // Update the count of bytes successfully received

        } while (buf[recv_bytes - 1] != '\n'); // Keep reading until the newline character is found

        if (DEBUG) fprintf(stderr, "Received answer of %d bytes...\n", recv_bytes);

        printf("Server response: %s\n", buf);
    }

    /*
     * Close the socket and release resources.
     */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close the socket");

    if (DEBUG) fprintf(stderr, "Socket closed...\n");

    if (DEBUG) fprintf(stderr, "Exiting...\n");

    exit(EXIT_SUCCESS);
}