/*
 * This is part of the first session.
 * 
 * Goals:
 * - review the basics of multithreading programming (by creating a multithreading application)
 * - comprehend the problems of concurrent access
 * - fix the problem
 * 
 * This session will teach how to use semaphores in C, by answering the following questions:
 * - how to implement mutual exclusion for critical section access?
 * - what is the value of semaphore overhead?
 * - how to implement mutual exclusion access for N distinct resources?
 *
 * Exercise 3 - Mutual exclusion access for N distinct resources
 * 
 * The goal is to manage concurrent access to a limited pool of N distinct resources.
 * M threads are launched simultaneously, but only N resources are available (with M > N).
 * How should this be handled?
 * 
 * by using a counting semaphore initialized to N, allowing up to N threads 
 * to enter the critical section concurrently before forcing others to wait.
 */

#include <errno.h>      // contains the global variable errno to determine the type of an error
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     // strerror() formats errno into a human-readable string
#include <unistd.h>     // sleep()

/* some constants, which may be fine-tuned for testing different scenarios */
#define MAX_SLEEP       3   // used to simulate a work item (max length)
#define NUM_RESOURCES   3   // number of available special resources
#define NUM_TASKS       3   // we define the number of work items per thread
#define THREAD_BURST    5   // determines how many threads are spawned at the same time

/* 
 * Unlike exercise 2 where there was only 1 basket, here there are NUM_RESOURCES 
 * baskets (e.g. 3) but a burst of THREAD_BURST cats (e.g. 5).
 * The semaphore acts as a counter of free baskets. The first 3 cats take a basket 
 * each and decrement the counter. The 4th cat finds the counter at 0 (or negative) 
 * and goes to sleep in the queue until a basket is freed by a departing cat.
 */

/* We use a simple structure to encapsulate a thread's arguments */
typedef struct thread_args_s {
    int     ID;
    sem_t*  semaphore;
    int     num_tasks;
} thread_args_t;


/* This is the function executed when a client thread is created */
void* client(void* arg_ptr) {
    thread_args_t* args = (thread_args_t*) arg_ptr;
    
    int i, ret = 0;
    
    /* 
     * sem_wait is the entry door. 
     * It decrements the counting semaphore. If the value drops below zero, it means 
     * all NUM_RESOURCES are currently busy, and the thread is put to sleep in the queue.
     */
    ret = sem_wait(args->semaphore);
    
    /* error handling */
    if (ret) {
        fprintf(stderr,"Lock error\n");
        exit(1);
    }
    
    printf("[@Thread%d] Resource acquired...\n", args->ID);
    
    /* 
     * Here begins the critical section,
     * process the work items assigned to the thread 
     */
    for (i = 0; i < args->num_tasks; ++i) {
        // we simulate a work item by sleeping for 0 up to MAX_SLEEP seconds
        sleep(rand() % (MAX_SLEEP+1));
    }
    
    /* 
     * sem_post is the exit door. 
     * It increments the counting semaphore, signaling that a resource 
     * is available again. If threads are sleeping in the queue, one is woken up.
     */
    ret = sem_post(args->semaphore);
    
    /* error handling */
    if (ret) {
        fprintf(stderr,"Unlock error\n");
        exit(1);
    }
    
    printf("[@Thread%d] Done. Resource released!\n", args->ID);
    
    free(args); /* dynamically allocated thread arguments must be freed to prevent memory leaks */
    return NULL;
}

int main(int argc, char* argv[]) {
    printf("Welcome! This is a very simple resource scheduler.\n\n");
    printf("We are simulating a system with %d available special resources. Hence, no more "
    "than %d threads can get exclusive access to them at the same time.\n\n", NUM_RESOURCES, NUM_RESOURCES);
    
    int ret = 0;
    int thread_ID = 0;
    
    sem_t* semaphore = malloc(sizeof(sem_t)); /* semaphore object allocated on the heap */
    
    /* 
     * sem_init initializes the semaphore.
     * The third parameter is initialized to NUM_RESOURCES instead of 1. 
     * This creates a "counting semaphore" rather than a "binary mutex", allowing 
     * up to NUM_RESOURCES threads to bypass the lock concurrently.
     */
    ret = sem_init(semaphore, 0, NUM_RESOURCES);
    
    if (ret) {
        fprintf(stderr,"Error\n");
        exit(1);
    }
    
    
    /* Main loop, waits for user input to spawn thread bursts */
    printf("[DRIVER] Press ENTER to spawn %d new threads. Press CTRL+D to quit!\n", THREAD_BURST);
    
    while(1) {
        int input_char;
        
        /* We want to skip any character that is not allowed:
         * - when ENTER is pressed, on Linux the character '\n' is read by getchar()
         * - CTRL+D is read as EOF, a special sequence defined in stdio.h */
        while ( (input_char = getchar()) != '\n' && input_char != EOF ) continue;
        
        if (input_char == EOF) break;
        
        printf("==> [DRIVER] Spawning %d threads now...\n", THREAD_BURST);
        
        int i;
        for (i = 0; i < THREAD_BURST; ++i) {
            pthread_t thread_handle;
            
            thread_args_t* args = malloc(sizeof(thread_args_t));
            args->semaphore = semaphore;
            args->ID = thread_ID;
            args->num_tasks = NUM_TASKS;
            
            if (pthread_create(&thread_handle, NULL, client, args)) {
                printf("==> [DRIVER] FATAL ERROR: cannot create thread %d: %s\nExiting...\n", thread_ID, strerror(errno));
                exit(1);
            }
            
            ++thread_ID;
            
            /* 
             * Since the main thread operates in an infinite loop waiting for user input, 
             * it cannot synchronously wait (pthread_join) for each child to terminate. 
             * Detaching the threads (pthread_detach) allows the OS scheduler to 
             * automatically reclaim their memory resources upon termination.
             */
            pthread_detach(thread_handle);
        }
        
        printf("==> [DRIVER] Press ENTER to spawn %d new threads. Press CTRL+D to quit!\n", THREAD_BURST);
    }
    
    printf("Exiting...\n");
    
    /* always destroy the semaphore once you're done to free system resources */
    sem_destroy(semaphore);
    
    free(semaphore);
    
    pthread_exit(NULL);
}
