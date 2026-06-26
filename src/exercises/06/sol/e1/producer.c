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

#define _GNU_SOURCE
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
 * Initialize FIFO
 *
 * Requests the kernel to create a FIFO and open it in write-only mode.
 * The internal pipe capacity is intentionally limited using fcntl and F_SETPIPE_SZ
 * to demonstrate buffer saturation.
 */
void initFIFO() {
    int ret = mkfifo(FIFO_NAME, 0666);
    if(ret) handle_error("Cannot create FIFO");

    fifo = open(FIFO_NAME, O_WRONLY);
    if(fifo == -1) handle_error("Cannot open FIFO for writing");
    
    /* Resize the FIFO buffer in the kernel */
    fcntl(fifo, F_SETPIPE_SZ, 10 * sizeof(int));
}

/*
 * Close FIFO
 *
 * Closes the local descriptor. The producer does not unlink the FIFO here
 * because the consumer (which runs last) is responsible for destroying it.
 */
static void closeFIFO() {
    int ret = close(fifo);
    if(ret) handle_error("Cannot close FIFO");
}
    
/* Generates a number between -MAX_TRANSACTION and +MAX_TRANSACTION */
static inline int performRandomTransaction() {
    struct timespec pause = {0};
    pause.tv_nsec = 10000000; /* 10 ms (100*10^6 ns) */
    nanosleep(&pause, NULL);

    int amount = rand() % (2 * MAX_TRANSACTION); /* {0, ..., 2*MAX_TRANSACTION - 1} */
    if (amount >= MAX_TRANSACTION) {
        return MAX_TRANSACTION - (amount + 1); /* {-MAX_TRANSACTION, ..., -1} */
    } else {
        return amount + 1; /* {1, ..., MAX_TRANSACTION} */
    }
}

/*
 * Write Value
 *
 * Sends a single integer through the FIFO descriptor.
 * Uses a while loop to ensure that all bytes are written, handling partial
 * writes and system interruptions (EINTR).
 */
int writeValue(int value) {
    int ret;
    int bytes_sent = 0;
    
    while (bytes_sent != sizeof(int)) {
        ret = write(fifo, &value, sizeof(int));
        
        /* Interrupt handling */
        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("Cannot write to FIFO");
        
        /* Check for partial write anomalies */
        if (ret != sizeof(int)) handle_error_en(ret, "Partial write to FIFO");
        
        bytes_sent = ret;
    }
    return bytes_sent;
}

void produce(int id, int numOps) {
    int localSum = 0;
    while (numOps > 0) {
        /* Generate data */
        int value = performRandomTransaction();

        /* Send the value through the FIFO descriptor */
        writeValue(value);

        localSum += value;
        numOps--;
    }
    printf("Producer %d ended. Local sum is %d\n", id, localSum);
}

int main(int argc, char** argv) {
    srand(PRNG_SEED);
    
    /* Global resources preparation */
    initFIFO();

    int i, ret;
    for (i = 0; i < NUM_PRODUCERS; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            handle_error("fork");
        } else if (pid == 0) {
            produce(i, OPS_PER_PRODUCER);
            _exit(EXIT_SUCCESS);
        }
    }

    for (i = 0; i < NUM_PRODUCERS; ++i) {
        int status;
        ret = wait(&status);
        if (ret == -1) handle_error("wait");
        if (WEXITSTATUS(status)) handle_error_en(WEXITSTATUS(status), "child crashed");
    }

    printf("Producers have terminated. Exiting...\n");
    
    /* Local cleanup */
    closeFIFO();

    exit(EXIT_SUCCESS);
}
