/*
 * This is part of the third session.
 *
 * Goals:
 * - implement a multiprocess application with shared memory
 * - solve the producer-consumer (M/N) problem with shared memory
 * - solve the producer-consumer (1/1) problem with shared memory without semaphores
 *
 * Exercise 3 - Producer-consumer (1/1) without semaphores
 * 
 * The goal is to develop a 1-to-1 producer-consumer application
 * that shares a circular buffer using POSIX shared memory, but without
 * using any kernel-level synchronization primitives (like semaphores).
 * 
 * But how is the execution coordinated?
 * 
 * by employing "busy waiting" on the shared indexes:
 * - The producer spins in a while loop until there is at least one free slot.
 * - The consumer spins in a while loop until there is at least one new item.
 * 
 * pros and cons of this approach:
 * [+] Very fast! No system calls (like sem_wait) are needed to synchronize.
 * [-] High CPU usage, the while loop continuously burns CPU cycles.
 * [-] Sacrifices one buffer slot to distinguish between "full" and "empty",
 * the buffer can hold a maximum of (BUFFER_SIZE - 1) elements.
 * 
 * Note: the producer acts as the initiator, so it must create and initialize
 * the shared memory. It is also responsible for unlinking it at the end.
 * 
 * Obviously, the producer must be launched before the consumer.
 */

#include <string.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>       // nanosleep()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "common.h"

/* 
 * Shared memory structure
 */
struct shared_memory {
    int buf [BUFFER_SIZE];
    int read_index;
    int write_index;
};

/* 
 * Global resources
 */
struct shared_memory *myshm_ptr; /* pointer to access the struct in shared memory */
int fd_shm; /* file descriptor for the shared memory object */

/* initialization logic */
void openMemory() {
    /* connect to existing shared memory */
    if ((fd_shm = shm_open (SH_MEM_NAME, O_RDWR, 0660)) == -1) handle_error("shm_open error");

    /* map the existing memory object into the consumer's virtual address space */
    if ((myshm_ptr = mmap (NULL, sizeof(struct shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED) handle_error ("mmap error");
}

void closeMemory() {
    int ret;

    /* Unmap the shared memory from local address space */
	ret = munmap(myshm_ptr, sizeof(struct shared_memory));
	if (ret == -1) handle_error("munmap error");

    /* Close the file descriptor */
    close(fd_shm);

    /*
     * no shm_unlink() here! 
     * In this 1:1 setup, the producer is responsible for destroying the shared memory.
     */
}

/* consumer logic */
void consume(int id, int numOps) {
    int localSum = 0, next_pos;

    while (numOps > 0) {
        /*
         * Busy waiting
         *
         * Instead of sleeping (sem_wait), the CPU continuously evaluates this condition.
         * Check if the buffer is empty.
         * If read_index == write_index, there is nothing to read.
         */
        while (myshm_ptr->read_index == myshm_ptr->write_index);
        
        /*
         * Critical section (safe without mutex!)
         *
         * why is this safe without sem_cs? 
         *
         * because there is only one consumer modifying read_index, and one producer
         * modifying write_index. They don't step on each other's toes.
         */
        int value = myshm_ptr->buf[myshm_ptr->read_index];
    	next_pos = (myshm_ptr->read_index + 1) % BUFFER_SIZE;

        /* 
         * update read_index only after the data is read.
         * This acts as a signal to the producer that a new slot is free.
         */
        myshm_ptr->read_index = next_pos;

        localSum += value;
        numOps--;
    }
    printf("Consumer %d ended. Local sum is %d\n", id, localSum);
}

int main(int argc, char** argv) {

    /* the consumer connects to the global resources */
    openMemory();

    /* 
     * no fork() here! 
     * This is a strict 1:1 setup. The main process acts directly as the consumer.
     */
    consume(0, OPS_PER_CONSUMER);

    printf("Consumer has terminated. Exiting...\n");

    /* local cleanup */
    closeMemory();

    exit(EXIT_SUCCESS);
}
