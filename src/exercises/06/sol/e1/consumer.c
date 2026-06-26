/*
 * This is part of the sixth session.
 *
 * Goals:
 * - learn to perform input and output operations using descriptors (fd) in UNIX
 * - implement IPC between unrelated processes using named pipes (FIFOs)
 * - implement communication through client-server architecture over TCP protocol
 *
 * Exercise 1 - Producer-Consumer with FIFO
 *
 * Producer
 * 
 * the prodcuer creates a named pipe (FIFO), sets its internal buffer size
 * to a fixed limit to cause earlier saturation, and forks NUM_PRODUCERS child processes.
 * Each child generates random integers and writes them into the FIFO.
 * The producer initiates the resources, so it must run first.
 * 
 * Consumer
 * 
 * the consumer connects to the existing FIFO created by the producer
 * and forks NUM_CONSUMERS child processes. Each child reads integers
 * from the FIFO. Since the consumer is the last process to operate,
 * it is responsible for destroying the FIFO resource from the system at the end.
 */

#include <string.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>       /* nanosleep() */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "common.h"

int fifo;

/*
 * Open FIFO
 *
 * Opens the existing FIFO in read-only mode.
 */
void openFIFO() {
    fifo = open(FIFO_NAME, O_RDONLY);
    if(fifo == -1) handle_error("Cannot open FIFO for reading");
}

/*
 * Close and remove FIFO
 *
 * Closes the local descriptor and unlinks the FIFO from the operating system.
 */
static void closeFIFO() {
    int ret = close(fifo);
    if(ret) handle_error("Cannot close FIFO");
    
    ret = unlink(FIFO_NAME);
    if(ret) handle_error("Cannot unlink FIFO");
}

/*
 * Read value
 *
 * Reads an integer from the FIFO descriptor.
 * Uses a while loop to handle partial reads and system interruptions (EINTR).
 * If 0 bytes are read, it interprets it as an unexpected closure of the pipe.
 */
int readValue(int * value) {
    int ret;
    int bytes_read = 0;
    
    do {
        /* Read data chunk */
        ret = read(fifo, value, sizeof(int));
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot read from FIFO");
        
        /* Unexpected EOF handling */
        if (ret == 0) handle_error("Process has closed the FIFO unexpectedly! Exiting...\n");
        
        /* Partial read handling */
        if (ret < sizeof(int)) handle_error_en(ret, "partial read from FIFO");
        
        bytes_read += ret;
    } while(bytes_read != sizeof(int));

    return bytes_read;
}

void consume(int id, int numOps) {
    int localSum = 0;
    int value;
    
    while (numOps > 0) {
        /* Read the message from FIFO */
        readValue(&value);
        
        localSum += value;
        numOps--;
    }
    printf("Consumer %d ended. Local sum is %d\n", id, localSum);
}

int main(int argc, char** argv) {
    
    /* Connect to existing resource */
    openFIFO();

    int i;
    for (i = 0; i < NUM_CONSUMERS; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            handle_error("fork");
        } else if (pid == 0) {
            consume(i, OPS_PER_CONSUMER);
            _exit(EXIT_SUCCESS);
        }
    }

    for (i = 0; i < NUM_CONSUMERS; ++i) {
        int status;
        wait(&status);
        if (WEXITSTATUS(status)) handle_error("child crashed");
    }

    printf("Consumers have terminated. Exiting...\n");
    
    /* Global cleanup */
    closeFIFO();
    
    exit(EXIT_SUCCESS);
}
