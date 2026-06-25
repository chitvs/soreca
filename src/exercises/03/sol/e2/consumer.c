/*
 * This is part of the third session.
 *
 * Goals:
 * - implement a multiprocess application with shared memory
 * - solve the producer-consumer (M/N) problem with shared memory
 * - solve the producer-consumer (1/1) problem with shared memory without semaphores
 *
 * Exercise 2 - Producer-consumer (M/N) with shared memory
 * 
 * The goal is to develop an application in two separate modules (producer-consumer)
 * that share a circular buffer using POSIX shared memory.
 * 
 * But how is the execution coordinated?
 * 
 * by employing multiple processes generated via fork():
 * - the producer acts as the initiator, so it must create the
 * semaphores and the shared memory object, initializing them.
 * - the consumer expects the shared memory and the semaphores to
 * already exist, so it just opens them.
 * 
 * In this exercise, the producer-consumer architecture is split:
 * 
 * - The main producer process creates named semaphores and the shared memory,
 * and spawns NUM_PRODUCERS child processes that act as concurrent writers.
 * - The main consumer process connects to the existing resources
 * and spawns NUM_CONSUMERS child processes that act as concurrent readers.
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
 *
 * Unlike previous labs where indexes were kept in a file or locally,
 * here the entire state of the buffer (data and indexes) is encapsulated
 * in a single struct that will be mapped into shared memory.
 */
struct shared_memory {
    int buf [BUFFER_SIZE];
    int read_index;
    int write_index;
};

/* 
 * Global resources
 * 
 * Inherited by child processes after the fork().
 */
struct shared_memory *myshm_ptr; /* pointer to access the struct in shared memory */
int fd_shm; /* file descriptor for the shared memory object */
sem_t *sem_empty, *sem_filled, *sem_cs; /* named semaphores for synchronization */

void openMemory() {
    /*
     * Connect to existing shared memory
     *
     * We do not use O_CREAT here. We expect the producer to have already 
     * created the memory object. We just open it with read/write permissions.
     */
    if ((fd_shm = shm_open (SH_MEM_NAME, O_RDWR, 0660)) == -1) 
        handle_error("shm_open error");
    
    /* map the existing memory object into the consumer's virtual address space */
    if ((myshm_ptr = mmap (NULL, sizeof(struct shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED) 
        handle_error ("mmap error");
}

/*
 * Local Cleanup
 *
 * the consumer unmaps and closes its local descriptor.
 */
void closeMemory() {
    int ret;
	ret = munmap(myshm_ptr, sizeof(struct shared_memory));
	if (ret == -1) handle_error("munmap error");
    
    close(fd_shm);
}

/*
 * Connect to buffer semaphores
 *
 * flag is 0, so that sem_open is not allowed to create them.
 * It just connects to the ones created by the producer.
 */
void openSemaphores() {
    sem_filled = sem_open(SEMNAME_FILLED, 0);
    if (sem_filled == SEM_FAILED) handle_error("sem_open filled");

    sem_empty = sem_open(SEMNAME_EMPTY, 0);
    if (sem_empty == SEM_FAILED) handle_error("sem_open empty");

    /*
     * Create consumer mutex
     *
     * here we DO use O_CREAT! 
     * The consumer module needs its own mutex to synchronize concurrent consumers. 
     * This is distinct from the producer's mutex.
     */
    sem_cs = sem_open(SEMNAME_CS_CONS, O_CREAT | O_EXCL, 0600, 1);
    if (sem_cs == SEM_FAILED) handle_error("sem_open cs cons");
}

void closeAndDestroySemaphores() {
    int ret;

    /* first close local semaphore descriptors... */
    ret = sem_close(sem_filled);
    if (ret) handle_error("sem_close filled");

    ret = sem_close(sem_empty);
    if (ret) handle_error("sem_close empty");

    ret = sem_close(sem_cs);
    if (ret) handle_error("sem_close cs");

    /*
     * ...then unlink, the consumer acts as the final step of the pipeline, so it destroys 
     * all semaphores from the OS, including the producer's mutex! 
     */
    ret = sem_unlink(SEMNAME_FILLED);
    if (ret) handle_error("sem_unlink filled");

    ret = sem_unlink(SEMNAME_EMPTY);
    if (ret) handle_error("sem_unlink empty");

    ret = sem_unlink(SEMNAME_CS_PROD);
    if (ret) handle_error("sem_unlink cs prod");

    ret = sem_unlink(SEMNAME_CS_CONS);
    if (ret) handle_error("sem_unlink cs cons");

}

/* consumer logic, executed by the child processes */
void consume(int id, int numOps) {
    int localSum = 0;

    /*
     * Entry section
     * 
     * Wait for at least one item to be available in the buffer (sem_filled).
     * Acquire the consumer mutex (sem_cs) to ensure mutual exclusion among consumers.
     */
    while (numOps > 0) {
        int ret = sem_wait(sem_filled);
        if (ret) handle_error("sem_wait filled");

        ret = sem_wait(sem_cs);
        if (ret) handle_error("sem_wait cs");
        
        /*
         * Critical section
         *
         * Read the value directly from the shared memory struct using the read_index.
         * Update the index using modulo arithmetic (circular buffer).
         */        
        int value = myshm_ptr->buf[myshm_ptr->read_index];
        myshm_ptr->read_index++;
        if (myshm_ptr->read_index == BUFFER_SIZE) myshm_ptr->read_index = 0;

        /*
         * Exit Section
         *
         * Release the consumer mutex (sem_cs).
         * Signal the producer(s) that a new slot is empty and available (sem_empty).
         */
        ret = sem_post(sem_cs);
        if (ret) handle_error("sem_post cs");

        ret = sem_post(sem_empty);
        if (ret) handle_error("sem_post empty");

        localSum += value;
        numOps--;
    }
    printf("Consumer %d ended. Local sum is %d\n", id, localSum);
}

int main(int argc, char** argv) {

    /* the main consumer connects to the global resources */
    openSemaphores();
    openMemory();

    int i;
    /* spawn NUM_PRODUCERS child processes */
    for (i=0; i<NUM_CONSUMERS; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            handle_error("fork");
        } else if (pid == 0) {
            /* child process executes the consume logic */
            consume(i, OPS_PER_CONSUMER);
            _exit(EXIT_SUCCESS);
        }
    }

    /* the parent process waits for all children to finish */
    for (i=0; i<NUM_CONSUMERS; ++i) {
        int status;
        wait(&status);
        if (WEXITSTATUS(status)) handle_error("child crashed");
    }

    printf("Consumers have terminated. Exiting...\n");

    /* global and local cleanup */
    closeAndDestroySemaphores();
    closeMemory();

    exit(EXIT_SUCCESS);
}
