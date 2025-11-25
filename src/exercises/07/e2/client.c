#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  // htons() and inet_addr()
#include <netinet/in.h> // struct sockaddr_in
#include <sys/socket.h>

#include "common.h"

int main(int argc, char* argv[]) {
    int ret,bytes_sent,recv_bytes;

    // variables for handling a socket
    int socket_desc;
    struct sockaddr_in server_addr = {0}; // some fields are required to be filled with 0

    /*
     * Create a UDP socket for contacting the server.
     * Uses AF_INET (IPv4) and SOCK_DGRAM (connectionless UDP protocol).
     */
    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_desc < 0)
        handle_error("Could not create socket");

    if (DEBUG) fprintf(stderr, "Socket created...\n");

    /*
     * Set up the destination server address parameters.
     * Configures the server address structure (IP, family, and port in network byte order).
     * Unlike TCP, no explicit connect() call is needed for UDP; the address is used in sendto().
     */
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); // don't forget about network byte order!

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;
    memset(buf,0,buf_len);

    // main loop
    while (1) {
        char* quit_command = SERVER_COMMAND;
        size_t quit_command_len = strlen(quit_command);

        printf("Insert your message: ");

        /* 
         * Read a line from stdin
         * fgets() reads up to sizeof(buf)-1 bytes and on success
         * returns the first argument passed to it. 
         */
        if (fgets(buf, sizeof(buf), stdin) != (char*)buf) {
            fprintf(stderr, "Error while reading from stdin, exiting...\n");
            exit(EXIT_FAILURE);
        }

        msg_len = strlen(buf);

        /*
         * Send the message to the server using UDP (sendto).
         * For UDP, one sendto() call typically sends the entire datagram.
         * The server address structure is passed explicitly.
         */
        bytes_sent=0;
        while ( bytes_sent < msg_len) {
            ret = sendto(socket_desc, buf, msg_len, 0, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent = ret;
        }

        if (DEBUG) fprintf(stderr, "Sent message of %d bytes...\n", bytes_sent);


        /*
         * Check if the sent command was the quit command.
         * If it matches SERVER_COMMAND, the client breaks the loop and closes the socket.
         */
        if (msg_len == quit_command_len && !memcmp(buf, quit_command, quit_command_len)){

            if (DEBUG) fprintf(stderr, "Sent QUIT command ...\n");
             break;
        }

        /*
         * Read the response message from the server using UDP (recvfrom).
         * This call blocks until a datagram is received. Since UDP is datagram-based,
         * the entire message is read in one go (up to buf_len).
         */
        recv_bytes = 0;
        do {
            ret = recvfrom(socket_desc, buf, buf_len, 0, NULL, NULL);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot read from the socket");
            if (ret == 0) break;
            recv_bytes = ret;

        } while ( recv_bytes<=0 );

        if (DEBUG) fprintf(stderr, "Received answer of %d bytes...\n",recv_bytes);

        printf("Server response: %s\n", buf); // no need to insert '\0'
    }

    /*
     * Close the socket descriptor and release resources.
     */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close the socket");

    if (DEBUG) fprintf(stderr, "Socket closed...\n");

    if (DEBUG) fprintf(stderr, "Exiting...\n");

    exit(EXIT_SUCCESS);
}