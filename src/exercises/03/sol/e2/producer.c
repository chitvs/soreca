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
    int buf[BUFFER_SIZE];
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

/* Initialization logic, executed by the main producer process */
void initMemory() {
    /*
     * Create and size the shared memory
     *
     * We request the kernel to create a shared memory object with O_CREAT and O_EXCL.
     * Then, we set its exact physical size to match our struct using ftruncate().
     */
    if ((fd_shm = shm_open (SH_MEM_NAME, O_RDWR | O_CREAT | O_EXCL, 0660)) == -1) 
        handle_error("shm_open error");

    if (ftruncate (fd_shm, sizeof (struct shared_memory)) == -1) 
        handle_error ("ftruncate error");

    /* Map the memory object into the virtual address space */
    if ((myshm_ptr = mmap (NULL, sizeof(struct shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)) == MAP_FAILED) 
        handle_error ("mmap error");

    /*
     * Initialize the shared memory to 0
     *
     * memset sets all bytes of the struct to 0. This is a crucial step because
     * it automatically initializes both read_index and write_index to 0.
     */
    memset(myshm_ptr, 0, sizeof(struct shared_memory));
}

/*
 * Cleanup shared memory
 *
 * The producer detaches the shared memory from its virtual address space (munmap),
 * closes the file descriptor, and finally unlinks the shared memory object.
 */
void closeMemory() {
    int ret;

	ret = munmap(myshm_ptr, sizeof(struct shared_memory));
	if (ret == -1) handle_error("munmap error");

    close(fd_shm);

	ret = shm_unlink(SH_MEM_NAME);
	if (ret == -1) handle_error("shm_unlink error");
}

/*
 * Create named semaphores
 *
 * We delete any stale semaphores first, then create them using O_CREAT and O_EXCL.
 * - sem_filled (init 0): tracks items ready to be consumed.
 * - sem_empty (init BUFFER_SIZE): tracks available slots.
 * - sem_cs (init 1): acts as a mutex specifically for concurrent producers.
 */
void initSemaphores() {
    // delete state semaphores from a previous crash (if any)
    sem_unlink(SEMNAME_FILLED);
    sem_unlink(SEMNAME_EMPTY);
    sem_unlink(SEMNAME_CS_PROD);
    sem_unlink(SEMNAME_CS_CONS);

    sem_filled = sem_open(SEMNAME_FILLED, O_CREAT | O_EXCL, 0600, 0);
    if (sem_filled == SEM_FAILED) handle_error("sem_open filled");

    sem_empty = sem_open(SEMNAME_EMPTY, O_CREAT | O_EXCL, 0600, BUFFER_SIZE);
    if (sem_empty == SEM_FAILED) handle_error("sem_open empty");

    sem_cs = sem_open(SEMNAME_CS_PROD, O_CREAT | O_EXCL, 0600, 1);
    if (sem_cs == SEM_FAILED) handle_error("sem_open cs prod");
}

/*
 * Cleanup semaphores
 * 
 * The producer closes its local semaphore descriptors.
 * It does not unlink them, so they remain available for the consumer.
 */
void closeSemaphores() {
    int ret = sem_close(sem_filled);
    if (ret) handle_error("sem_close filled");

    ret = sem_close(sem_empty);
    if (ret) handle_error("sem_close empty");

    ret = sem_close(sem_cs);
    if (ret) handle_error("sem_close cs");
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
    int localSum = 0;
    while (numOps > 0) {
        // producer, just do your thing!
        int value = performRandomTransaction();

        /*
         * Entry section
         *
         * Wait for an empty slot in the buffer (sem_empty).
         * Acquire the producer mutex (sem_cs) to ensure mutual exclusion among producers.
         */
        int ret = sem_wait(sem_empty);
        if (ret) handle_error("sem_wait empty\n");

        ret = sem_wait(sem_cs);
        if (ret) handle_error("sem_wait cs");

        /*
         * Critical section
         *
         * write value in the buffer inside the shared memory and update the producer position.
         * The write_index acts as a circular pointer (modulo BUFFER_SIZE).
         */
        myshm_ptr->buf[myshm_ptr->write_index] = value;
        myshm_ptr->write_index++;
        if (myshm_ptr->write_index == BUFFER_SIZE) myshm_ptr->write_index = 0;

        /*
         * Exit section
         *
         * Release the producer mutex (sem_cs).
         * Signal the consumer(s) that a new item is available (sem_filled).
         */
        ret = sem_post(sem_cs);
        if (ret) handle_error("sem_post cs");

        ret = sem_post(sem_filled);
        if (ret) handle_error("sem_post filled");

        localSum += value;
        numOps--;
    }
    printf("Producer %d ended. Local sum is %d\n", id, localSum);
}

int main(int argc, char** argv) {
    srand(PRNG_SEED);

    /* the main producer prepares the global resources */
    initSemaphores();
    initMemory();

    int i, ret;
    /* spawn NUM_PRODUCERS child processes */
    for (i=0; i<NUM_PRODUCERS; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            handle_error("fork");
        } else if (pid == 0) {
            /* child process executes the produce logic */
            produce(i, OPS_PER_PRODUCER);
            _exit(EXIT_SUCCESS);
        }
    }

    /* the parent process waits for all children to finish */
    for (i=0; i<NUM_PRODUCERS; ++i) {
        int status;
        ret = wait(&status);
        if (ret == -1) handle_error("wait");
        if (WEXITSTATUS(status)) handle_error_en(WEXITSTATUS(status), "child crashed");
    }

    printf("Producers have terminated. Exiting...\n");

    /* local cleanup */
    closeSemaphores();
    closeMemory();

    exit(EXIT_SUCCESS);
}
