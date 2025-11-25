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

    struct sockaddr_in client_addr;

    int sockaddr_len = sizeof(client_addr); // we will reuse it for accept()

    // echo loop
    while (1) {        
        /*
         * Receive the client's command or message using recvfrom.
         * The call blocks until a datagram is received, storing the sender's address
         * in client_addr, which is needed for the echo response.
         */
        recv_bytes = 0;
        do {
            recv_bytes = recvfrom(socket_desc, buf, buf_len, 0, (struct sockaddr*) &client_addr, (socklen_t*) &sockaddr_len);
            if (recv_bytes == -1 && errno == EINTR) continue;
            if (recv_bytes == -1) handle_error("Cannot read from the socket");
            // UDP sockets typically do not return 0 unless shutdown() is called
            if (recv_bytes == 0) break; 
        } while ( recv_bytes <= 0 );

        if (DEBUG) fprintf(stderr, "Received command of %d bytes...\n",recv_bytes);

        /*
         * Check if the received message is the quit command (SERVER_COMMAND).
         * For a UDP server, receiving a quit command typically just stops processing
         * the current message but does not terminate the connection handler, as UDP is connectionless.
         */
        if (recv_bytes == quit_command_len && !memcmp(buf, quit_command, quit_command_len)){

            if (DEBUG) fprintf(stderr, "Received QUIT command...\n");
            continue; // Continue listening for the next datagram
         }

        /*
         * Echo the received message back to the client using sendto.
         * The 'client_addr' structure captured by recvfrom is used as the destination.
         */
        bytes_sent=0;
        while ( bytes_sent < recv_bytes) {
            ret = sendto(socket_desc, buf, recv_bytes, 0, (struct sockaddr*) &client_addr, sockaddr_len);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent += ret;
        }
    }

    /*
     * Close the socket descriptor and release unused resources.
     * This section should only be reachable if the main loop is broken (which it is not in this UDP example).
     */
    ret = close(socket_desc);
    if (ret < 0) handle_error("Cannot close socket for incoming connection");

    if (DEBUG) fprintf(stderr, "Socket closed...\n");

    return NULL;
}

int main(int argc, char* argv[]) {
    int ret;
    int socket_desc, client_desc;

    // some fields are required to be filled with 0
    struct sockaddr_in server_addr = {0}, client_addr = {0};

    int sockaddr_len = sizeof(struct sockaddr_in); // we will reuse it for accept()

    /*
     * Create a socket for listening (UDP socket).
     * Uses AF_INET (IPv4) and SOCK_DGRAM (connectionless UDP).
     */
    socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_desc < 0)
        handle_error("Could not create socket");

    if (DEBUG) fprintf(stderr, "Socket created...\n");

    /* 
     * We enable SO_REUSEADDR to quickly restart our server after a crash:
     * for more details, read about the TIME_WAIT state in the TCP protocol 
     */
    int reuseaddr_opt = 1;
    ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    if (ret < 0)
        handle_error("Cannot set SO_REUSEADDR option");

    /*
     * Configure server address and bind it to the UDP socket.
     * Sets IP to INADDR_ANY (accepting datagrams on all interfaces) and 
     * converts the port to network byte order using htons().
     */
    server_addr.sin_addr.s_addr = INADDR_ANY; // we want to accept connections from any interface
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); // don't forget about network byte order!

    ret = bind(socket_desc, (struct sockaddr*) &server_addr, sockaddr_len);
    if (ret < 0)
        handle_error("Cannot bind address to socket");

    if (DEBUG) fprintf(stderr, "Binded address to socket...\n");

    while (1) {
        /*
         * For UDP, there is no accept() call. The server immediately enters the 
         * connection_handler, passing the listening socket descriptor where recvfrom() 
         * will handle incoming datagrams from any client.
         */
        if (DEBUG) fprintf(stderr, "opening connection handler...\n");

        connection_handler(socket_desc);
    }

    exit(EXIT_SUCCESS); // this will never be executed
}