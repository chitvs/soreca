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
#include <semaphore.h> /* useless */
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
 *
 * The state of the buffer (data and indexes) is encapsulated
 * in a single struct mapped into shared memory.
 */
struct shared_memory {
    int buf[BUFFER_SIZE];
    int read_index;
    int write_index;
};

/* 
 * Global resources
 */
struct shared_memory *myshm_ptr; /* pointer to access the struct in shared memory */
int fd_shm; /* file descriptor for the shared memory object */

/* Initialization logic, executed by the main producer process */
void initMemory() {
    
    /* 
     * Preventive unlink (clear any stale memory from previous crashes 
     * before attempting to create a new one).
     */
    shm_unlink(SH_MEM_NAME);

    /* Request the kernel to create a shared memory object */
    if ((fd_shm = shm_open(SH_MEM_NAME, O_RDWR | O_CREAT, 0660)) == -1) 
        handle_error("shm_open error");
    
    /* Set its exact physical size */
    if (ftruncate(fd_shm, sizeof(struct shared_memory)) == -1) 
        handle_error("ftruncate error");
    
    /* Map the memory object into the virtual address space */
    if ((myshm_ptr = mmap(NULL, sizeof(struct shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED) 
        handle_error("mmap error");

    /* 
     * Initialize the shared memory to 0. 
     * This ensures read_index and write_index start exactly at 0.
     */
    memset(myshm_ptr, 0, sizeof(struct shared_memory));
}

void closeMemory() {
    int ret;
    
    /* Unmap the shared memory from local address space */
    ret = munmap(myshm_ptr, sizeof(struct shared_memory));
    if (ret == -1) handle_error("munmap error");
    
    /* Close the file descriptor */
    close(fd_shm);
    
    /* 
     * Destroy the shared memory object globally.
     */
    ret = shm_unlink(SH_MEM_NAME);
    if (ret == -1) handle_error("shm_unlink error");
}

// generates a number between -MAX_TRANSACTION and +MAX_TRANSACTION
static inline int performRandomTransaction() {
    struct timespec pause = {0};
    pause.tv_nsec = 10000000; // 10 ms (100*10^6 ns)
    nanosleep(&pause, NULL);

    int amount = rand() % (2 * MAX_TRANSACTION); // {0, ..., 2*MAX_TRANSACTION - 1}
    if (amount >= MAX_TRANSACTION) {
        return MAX_TRANSACTION - (amount+1); // {-MAX_TRANSACTION, ..., -1}
    } else {
        return amount + 1; // {1, ..., MAX_TRANSACTION}
    }
}

/* producer logic, executed by child processes */
void produce(int id, int numOps) {
    int localSum = 0, next_pos = 0;
    while (numOps > 0) {
        // producer, just do your thing!
        int value = performRandomTransaction();
       
        /*
         * Busy wainting
         *
         * Instead of sleeping (sem_wait), the CPU continuously evaluates this condition.
         * We check if writing would make write_index "lap" (catch up to) read_index.
         * If (write_index + 1) % SIZE == read_index, the buffer is considered full.
         */
        while ((myshm_ptr->write_index + 1) % BUFFER_SIZE == myshm_ptr->read_index);
        
        /*
         * Critical section (safe without mutex!)
         *
         * why is this safe without sem_cs? 
         *
         * because there is only one producer modifying write_index, and one consumer
         * modifying read_index. They don't step on each other's toes.
         */
        myshm_ptr->buf[myshm_ptr->write_index] = value;
        next_pos = (myshm_ptr->write_index + 1) % BUFFER_SIZE;
        
        /* 
         * we update write_index only after the data is written.
         * This acts as a signal to the consumer that new data is ready.
         */
        myshm_ptr->write_index = next_pos;

        localSum += value;
        numOps--;
    }
    printf("Producer %d ended. Local sum is %d\n", id, localSum);
}

int main(int argc, char** argv) {
    srand(PRNG_SEED);
    
    /* The producer prepares the global resources */
    initMemory();

    /* 
     * no fork() here! 
     * This is a strict 1:1 setup. The main process acts directly as the producer.
     */
    produce(0, OPS_PER_PRODUCER);

    printf("Producer has terminated. Exiting...\n");
    
    /* Wait for a moment to ensure the consumer finishes reading before we nuke the memory */
    sleep(1); 
    closeMemory();

    exit(EXIT_SUCCESS);
}
