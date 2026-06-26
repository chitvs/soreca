#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> // mkfifo()
#include <sys/stat.h>  // mkfifo()

#include "common.h"

int readOneByOne(int fd, char* buf, char separator);
void writeMsg(int fd, char* buf, int size);

static void cleanFIFOs(int echo_fifo, int client_fifo) {

    // close the descriptors
    int ret = close(echo_fifo);
    if(ret) handle_error("Cannot close Echo FIFO");
    ret = close(client_fifo);
    if(ret) handle_error("Cannot close Client FIFO");

    // destroy the two FIFOs
    ret = unlink(ECHO_FIFO_NAME);
    if(ret) handle_error("Cannot unlink Echo FIFO");
    ret = unlink(CLNT_FIFO_NAME);
    if(ret) handle_error("Cannot unlink Client FIFO");
}

/** Echo component **/
int main(int argc, char* argv[]) {
    int ret;
    int echo_fifo, client_fifo;
    char buf[1024];

    char* quit_command = QUIT_COMMAND;
    size_t quit_command_len = strlen(quit_command);

    // Create the two FIFOs
    unlink(ECHO_FIFO_NAME);
    unlink(CLNT_FIFO_NAME);
    ret = mkfifo(ECHO_FIFO_NAME, 0666);
    if(ret) handle_error("Cannot create Echo FIFO");
    ret = mkfifo(CLNT_FIFO_NAME, 0666);
    if(ret) handle_error("Cannot create Client FIFO");

    echo_fifo = open(ECHO_FIFO_NAME, O_WRONLY);
    if(echo_fifo == -1) handle_error("Cannot open Echo FIFO for writing");
    client_fifo = open(CLNT_FIFO_NAME, O_RDONLY);
    if(client_fifo==-1) handle_error("Cannot open Client FIFO for reading");

    // send welcome message
    sprintf(buf, "Hi! I'm an Echo process based on FIFOs. I will send you back through a FIFO whatever"
            " you send me through the other FIFO, and I will stop and exit when you send me %s.\n", quit_command);

    writeMsg(echo_fifo, buf, strlen(buf));

    while (1) {
        memset(buf,0,1024);
        int bytes_read = readOneByOne(client_fifo, buf, '\n');

        if (DEBUG) {
            buf[bytes_read] = '\0';
            printf("Message received: %s", buf);
        }

        // check whether I have just been told to quit...
        if (bytes_read == quit_command_len && !memcmp(buf, quit_command, quit_command_len)) break;

        // ... or if I have to send the message back through the Echo FIFO
        writeMsg(echo_fifo, buf, bytes_read);
    }

    // close the descriptors and destroy the two FIFOs
    cleanFIFOs(echo_fifo, client_fifo);
    exit(EXIT_SUCCESS);
}