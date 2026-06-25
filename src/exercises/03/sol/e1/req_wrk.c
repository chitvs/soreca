/*
 * This is part of the third session.
 *
 * Goals:
 * - implement a multiprocess application with shared memory
 * - solve the producer-consumer (M/N) problem with shared memory
 * - solve the producer-consumer (1/1) problem with shared memory without semaphores
 *
 * Exercise 1 - Multiprocess application with shared memory
 * 
 * The goal is to develop an application in two components (requester
 * and worker) that share data using POSIX shared memory.
 * 
 * But how is the execution coordinated?
 * 
 * by employing two processes generated via fork():
 * - the requester loads data into shared memory and prints the final result
 * - the worker processes the data in the shared memory
 * 
 * ultimately 2 named semaphores act as signals to enforce the correct execution order
 */

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <pthread.h>

/* 
 * Global resources
 *
 * These variables are declared globally so they are inherited by the child
 * process after the fork(). Even though the parent and child have separate
 * memory spaces, the values they hold (the file descriptor and semaphore pointers)
 * refer to the exact same OS-level resources.
 */

int *data; /* the pointer used to access the mapped shared memory area as a standard array */
int fd; /* the file descriptor returned by shm_open representing the shared memory object */
sem_t *sem_worker, *sem_request; /* named semaphores used exclusively to coordinate the execution flow (ping-pong) */

/* requester logic, executed by the parent process */
int request() {

    /*
     * Memory mapping
     *
     * mmap() asks the kernel to map the shared memory object identified by the 'fd'
     * into this process's virtual address space.
     * 
     * PROT_READ and PROT_WRITE -> access in both reading and writing
     * MAP_SHARED ensures that any modification to 'data' is instantly visible
     * to any other process mapping this same 'fd'.
     */
    if ((data = (int *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) handle_error("mmap error");

    printf("request: mapped address: %p\n", data);

    /* data generation */
    int i;
    for (i = 0; i < NUM; ++i){
        data[i] = i;
    }

    printf("request: data generated\n");
    
    /*
     * Synchronization
     * 
     * Since the worker is waiting on 'sem_worker', we wake it up
     * by posting its semaphore. Hey, wake up, data is ready!!!
     * 
     * Immediately after, we wait on 'sem_request' until the worker
     * finishes the elaboration and wakes us up.
     * 
     */
    if (sem_post(sem_worker) != 0) handle_error("sem_post error, sem: sem_worker");
    if (sem_wait(sem_request) != 0) handle_error("sem_wait error, sem: sem_request");

    /* at this point, the worker has finished, posted sem_request, and we are awake again */
    printf("request: acquire updated data\n");
    printf("request: updated data:\n");

    for (i = 0; i < NUM; ++i){
        printf("%d\n", data[i]);
    }
    
    /*
     * Cleanup
     * 
     * let's release the resources.
     * munmap() detaches the shared memory from this specific process's virtual memory.
     * It closes our window to the shared RAM, but it does not destroy the shared memory
     * object in the OS.
     */
    if (munmap(data, SIZE) == -1) handle_error("munmap error");

    return EXIT_SUCCESS;
}

/* worker logic, executed by the child process */
int work() {
    
    /*
     * Memory mapping
     * 
     * The child process also maps the same 'fd' into its virtual space.
     * The printed address might be different from the requester's one,
     * but they point to the exact same physical RAM block.
     */
    if ((data = (int *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) handle_error("mmap error");
    
    printf("worker: mapped address: %p\n", data);
    
    /*
     * Synchronization
     * 
     * The worker immediately goes to sleep waiting for the requester
     * to generate the initial data and post on 'sem_worker'.
     */
    if (sem_wait(sem_worker) != 0) handle_error("sem_wait error, sem: sem_worker");

    printf("worker: waiting initial data\n");
    printf("worker: initial data acquired\n");
    printf("worker: update data\n");
    
    int i;
    for (i = 0; i < NUM; ++i){
        data[i] = data[i] * data[i];
    }

    printf("worker: release updated data\n");
    
    /*
     * Synchronization
     * 
     * signal the requester that the elaboration is completed so it can
     * wake up and print the results.
     */
    if (sem_post(sem_request) != 0) handle_error("sem_post error, sem: sem_request");
    
    /*
     * Cleanup
     * 
     * clean up the local mapping before exiting.
     */
    if (munmap(data, SIZE) == -1) handle_error("munmap error");

    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    
    /*
     * Preventive unlink
     *
     * Clear any stale resources left by previous crashes to ensure
     * a clean slate before creating new ones.
     */
    sem_unlink(SEM_NAME_REQ);
    sem_unlink(SEM_NAME_WRK);
    shm_unlink(SHM_NAME);

    /*
     * Create named semaphores
     *
     * We create the semaphores with O_CREAT and O_EXCL.
     * They are explicitly initialized to 0 because they are used as 
     * synchronization signals, not as mutexes.
     */
    sem_request = sem_open(SEM_NAME_REQ, O_CREAT | O_EXCL, 0600, 0);
    if (sem_request == SEM_FAILED) handle_error("sem_open error, sem: sem_request");

    sem_worker = sem_open(SEM_NAME_WRK, O_CREAT | O_EXCL, 0600, 0);
    if (sem_worker == SEM_FAILED) handle_error("sem_open error, sem: sem_worker");

    /*
     * Create shared memory object
     *
     * shm_open() creates a POSIX shared memory object in the OS and returns
     * a file descriptor. O_RDWR gives us read and write permissions.
     */
    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) handle_error("shm_open error");

    /*
     * Sizing the memory
     *
     * A newly created shared memory object has a size of 0 bytes.
     * ftruncate() is mandatory to allocate the actual physical bytes needed.
     */
    if (ftruncate(fd, SIZE) == -1) handle_error ("ftruncate error");

    /*
     * Process creation
     *
     * fork() creates a child. The child inherits a copy of the parent's
     * variables, meaning it gets the exact same 'fd' and semaphore pointers.
     */
    int ret;
    pid_t pid = fork();
    if (pid == -1){
        handle_error("main: fork");
    }
    else if (pid == 0){
        /* child process becomes the worker */
        work();
        _exit(EXIT_SUCCESS);
    }

    /* Parent process becomes the requester */
    request();

    /*
     * Synchronization (process level)
     *
     * The parent waits for the child process to terminate completely
     * before proceeding to the global cleanup.
     */
    int status;
    ret = wait(&status);
    if (ret == -1)
        handle_error("main: wait");
    if (WEXITSTATUS(status))
        handle_error_en(WEXITSTATUS(status), "request() crashed");
    
    /*
     * Global cleanup
     *
     * The parent process is responsible for destroying the IPC structures from the OS.
     * - Close local semaphore pointers (sem_close)
     * - Unlink semaphores globally (sem_unlink)
     * - Close the shared memory file descriptor (close)
     * - Unlink the shared memory object globally (shm_unlink)
     */
    ret = sem_close(sem_worker);
    if (ret) handle_error("sem_close error, sem: sem_worker");
    ret = sem_close(sem_request);
    if (ret) handle_error("sem_close error, sem: sem_request");

    ret = sem_unlink(SEM_NAME_REQ);
    if (ret) handle_error("sem_unlink error, sem: sem_request");
    ret = sem_unlink(SEM_NAME_WRK);
    if (ret) handle_error("sem_unlink error, sem: sem_worker");

    ret = close(fd);
	if (ret == -1) handle_error("close error");

    ret = shm_unlink(SHM_NAME);
    if (ret) handle_error("shm_unlink error");

    _exit(EXIT_SUCCESS);
}
