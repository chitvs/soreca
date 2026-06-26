/*
 * This is part of the eighth session.
 *
 * Goals:
 * - Understand the C10K Problem and server scalability bottlenecks.
 * - Implement Server Parallelism using Multi-Process architecture (fork).
 * - Implement Server Parallelism using Multi-Thread architecture (pthread).
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
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>  /* htons() */
#include <netinet/in.h> /* struct sockaddr_in */
#include <sys/socket.h>

#include "common.h"

#ifdef SERVER_SERIAL
/* Nothing to do here */
#elif SERVER_MPROC
/* Nothing to do here */
#elif SERVER_MTHREAD
#include <pthread.h>
/*
 * Fields for the arguments that will be populated in the main thread 
 * and then accessed in thread_connection_handler().
 */
typedef struct handler_args_s
{
    int socket_desc;
    struct sockaddr_in* client_addr;
} handler_args_t;
#endif

void connection_handler(int socket_desc, struct sockaddr_in* client_addr) {
    int ret, recv_bytes;

    char buf[1024];
    size_t buf_len = sizeof(buf);
    int msg_len;

    char* quit_command = SERVER_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    /* Parse the ip address of the client and the port number of the client */
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(client_addr->sin_port);
    
    /* Send welcome message */
    sprintf(buf, "Hi! I'm an echo server. You are %s talking on port %hu.\nI will send you back whatever"
            " you send me. I will stop if you send me %s :-)\n", client_ip, client_port, quit_command);
    msg_len = strlen(buf);
    
    int bytes_sent = 0;
    while (bytes_sent < msg_len) {
        ret = send(socket_desc, buf + bytes_sent, msg_len - bytes_sent, 0);
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to the socket");
        bytes_sent += ret;
    }

    while (1) {
        /* Read message from client */
        memset(buf, 0, buf_len);
        recv_bytes = 0;
        do {
            ret = recv(socket_desc, buf + recv_bytes, 1, 0);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot read from the socket");
            if (ret == 0) break;
        } while ( buf[recv_bytes++] != '\n' );

        if (recv_bytes == 0) break;
        
        /* Check if it is the quit message, in that case exit from the loop */
        if (recv_bytes == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) break;

        /* Otherwise send back to the client the received message */
        bytes_sent = 0;
        while (bytes_sent < recv_bytes) {
            ret = send(socket_desc, buf + bytes_sent, recv_bytes - bytes_sent, 0);
            if (ret == -1 && errno == EINTR) continue;
            if (ret == -1) handle_error("Cannot write to the socket");
            bytes_sent += ret;
        }
    }

    /* Close socket */
    ret = close(socket_desc);
    if(ret) handle_error("Cannot close socket for incoming connection");
}

#ifdef SERVER_SERIAL

void serialServer(int server_desc) {
    /* Initialize client_addr to zero */
    struct sockaddr_in client_addr = {0};

    /* Loop to handle incoming connections serially */
    int sockaddr_len = sizeof(struct sockaddr_in);
    while (1) {
        int client_desc = accept(server_desc, (struct sockaddr*) &client_addr, (socklen_t*) &sockaddr_len);
        if(client_desc == -1 && errno == EINTR) continue; /* Check for interruption by signals */
        if(client_desc < 0) handle_error("Cannot open socket for incoming connection");

        if (DEBUG) fprintf(stderr, "Incoming connection accepted...\n");

        /*
         * Pass the socket descriptor and the address information
         * for the incoming connection to the handler. 
         */
        connection_handler(client_desc, &client_addr);

        if (DEBUG) fprintf(stderr, "Done!\n");

        /* Reset fields in client_addr */
        memset(&client_addr, 0, sizeof(struct sockaddr_in));
    }
}

#elif SERVER_MPROC

void mprocServer(int server_desc) {
    int ret = 0;
    /* Initialize client_addr to zero */
    struct sockaddr_in client_addr = {0};

    /* Loop to manage incoming connections forking the server process */
    int sockaddr_len = sizeof(struct sockaddr_in);
    while (1) {
        /* Accept incoming connection */
        int client_desc = accept(server_desc, (struct sockaddr *) &client_addr, (socklen_t *)&sockaddr_len);
        
        /* Check for interruption by signals */
        if(client_desc == -1 && errno == EINTR) continue; 
        if(client_desc < 0) handle_error("Cannot open socket for incoming connection");

        if (DEBUG) fprintf(stderr, "Incoming connection accepted...\n");
        
        /*
         * - Use fork() to create a child process to handle the request.
         * - Close descriptors that are not used in the parent and the child process.
         * Note that a connection is closed only when all the descriptors associated 
         * with it have been closed.
         * - connection_handler() is executed by the child process.
         * - memset() is performed in the parent to accept a new request.
         * - Added debug messages in parent and child.
         */
        pid_t pid = fork();
        if (pid < 0) {
            handle_error("Failed to fork server process for incoming connection");
        } 
        else if (pid == 0) {
            /* CHILD PROCESS */
            /* Close the listening socket; child doesn't accept new connections */
            ret = close(server_desc);
            if (ret) handle_error("Cannot close listening socket in child process");

            /* Handle the client connection */
            connection_handler(client_desc, &client_addr);

            if (DEBUG) fprintf(stderr, "Child process has finished handling the request.\n");

            /* Exit child process */
            _exit(EXIT_SUCCESS);
        } 
        else {
            /* PARENT PROCESS */
            /* Close the connected socket; parent doesn't handle this client */
            ret = close(client_desc);
            if (ret) handle_error("Cannot close client socket in parent process");

            if (DEBUG) fprintf(stderr, "Child process created to handle the request.\n");

            /* Reset client address to accept new connections */
            memset(&client_addr, 0, sizeof(struct sockaddr_in));
        }
    }
}

#elif SERVER_MTHREAD

/*
 * Wrapper method to call connection_handler from the thread.
 *
 * - Uses the struct handler_args_t to wrap the arguments.
 * - After calling connection_handler, frees all the resources used by the thread
 * and explicitly calls pthread_exit().
 */
void *thread_connection_handler(void *arg) {

    handler_args_t *args = (handler_args_t *)arg;
    int socket_desc = args->socket_desc;
    struct sockaddr_in *client_addr = args->client_addr;
    
    connection_handler(socket_desc, client_addr);

    free(args->client_addr);
    free(args);
    
    pthread_exit(NULL);
}

void mthreadServer(int server_desc) {
    int ret = 0;
    
    /* Allocate client_addr dynamically and initialize it to zero */
    struct sockaddr_in* client_addr = calloc(1, sizeof(struct sockaddr_in));

    /* Loop to manage incoming connections spawning handler threads */
    int sockaddr_len = sizeof(struct sockaddr_in);
    while (1) {
        /* Accept incoming connection */
        int client_desc = accept(server_desc, (struct sockaddr*) client_addr, (socklen_t*) &sockaddr_len);
        if(client_desc == -1 && errno == EINTR) continue; /* Check for interruption by signals */
        if(client_desc < 0) handle_error("Cannot open socket for incoming connection");

        if (DEBUG) fprintf(stderr, "Incoming connection accepted...\n");

        pthread_t thread;

        /*
         * - thread_connection_handler() is executed in the spawned thread.
         * - A handler_args_t data structure is allocated and populated with arguments.
         * - Since pthread_join() is not needed, pthread_detach() is used to release 
         * libpthread's internal resources.
         * - To safely accept a new connection while a thread is executing, client_addr 
         * is allocated dynamically inside the loop for future connections. Resetting 
         * fields in a shared client_addr would cause data races!
         */
        handler_args_t *thread_args = malloc(sizeof(handler_args_t));
        thread_args->socket_desc = client_desc;
        thread_args->client_addr = client_addr;

        ret = pthread_create(&thread, NULL, thread_connection_handler, (void *)thread_args);
        if (ret) handle_error_en(ret, "Could not create a new thread");
        
        if (DEBUG) fprintf(stderr, "New thread created to handle the request!\n");
        
        ret = pthread_detach(thread);
        if (ret) handle_error_en(ret, "Could not detach the thread");
        
        /* Allocate a fresh client_addr for the next incoming connection */
        client_addr = calloc(1, sizeof(struct sockaddr_in));
    }
}

#endif

int main(int argc, char* argv[]) {
    int ret;
    int socket_desc;

    /* Some fields are required to be filled with 0 */
    struct sockaddr_in server_addr = {0};

    /* Initialize socket for listening */
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
    if(socket_desc < 0) handle_error("Could not create socket");

    server_addr.sin_addr.s_addr = INADDR_ANY; /* Accept connections from any interface */
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT); /* Network byte order conversion */

    /*
     * Enable SO_REUSEADDR to quickly restart the server after a crash:
     * for more details, read about the TIME_WAIT state in the TCP protocol.
     */
    int reuseaddr_opt = 1;
    ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    if(ret) handle_error("Cannot set SO_REUSEADDR option");

    /* Bind address to socket */
    ret = bind(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    if(ret) handle_error("Cannot bind address to socket");

    /* Start listening */
    ret = listen(socket_desc, MAX_CONN_QUEUE);
    if(ret) handle_error("Cannot listen on socket");

#ifdef SERVER_MPROC
    mprocServer(socket_desc);
#elif SERVER_MTHREAD
    mthreadServer(socket_desc);
#elif SERVER_SERIAL
    serialServer(socket_desc);
#endif

    exit(EXIT_SUCCESS); /* This will never be executed */
}
