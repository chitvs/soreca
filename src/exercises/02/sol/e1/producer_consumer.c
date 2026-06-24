/*
 * This is part of the second session.
 * 
 * Goals:
 * - solve the producer-consumer problem (bounded buffer)
 * - implement inter-process synchronization (named semaphores)
 * - implement both, bounded buffer + named semaphores
 *
 * Exercise 1 - Producer-consumer with a bounded buffer
 * 
 * The goal is to sync NUM_PRODUCERS and NUM_CONSUMERS operating
 * concurrently on a shared bounded circular buffer.
 * 
 * But how is synchronization achieved?
 * 
 * by employing a 4-semaphore architecture:
 * 2 counting semaphores track the buffer's capacity and state
 * 2 binary semaphores ensure mutual exclusion for the read and
 * write indexes
 */

#include <string.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h> // nanosleep()
#include "common.h"

/* global variables, useful for testing different scenarios */
#define BUFFER_SIZE         128
#define INITIAL_DEPOSIT     0
#define MAX_TRANSACTION     1000
#define NUM_CONSUMERS       2
#define NUM_PRODUCERS       4
#define PRNG_SEED           0

#define NUM_OPERATIONS      400
#define OPS_PER_CONSUMER    (NUM_OPERATIONS/NUM_CONSUMERS)
#define OPS_PER_PRODUCER    (NUM_OPERATIONS/NUM_PRODUCERS)

// we use the preprocessor to check if our parameters are okay
#if OPS_PER_CONSUMER*NUM_CONSUMERS != OPS_PER_PRODUCER*NUM_PRODUCERS
#error "Choose NUM_CONSUMERS and NUM_PRODUCERS so that we get exactly NUM_OPERATIONS operations"
#endif

// struct used to specify arguments for a thread
typedef struct {
    int threadId;
    int numOps;
} thread_args_t;

// shared data

int transactions[BUFFER_SIZE]; /* bounded buffer, a circular array acting as a FIFO queue */
int deposit = INITIAL_DEPOSIT; /* shared variable */
int read_index, write_index; /* pointers for the circular buffer */

/* 
 * 4-semaphore architecture;
 * e and n will track the buffer's capacity and state
 * s1 and s2 will guarantee mutex
 * 
 * e -> tracks empty slots, prevents buffer overflow
 * n -> tracks filled slots, prevents buffer underflow
 * s1 -> ensures only one producer modifies write_index at a time
 * s2 -> ensures only one consumer modifies read_index at a time
 */
sem_t e, n, s1, s2;

// generates a number between -MAX_TRANSACTION and +MAX_TRANSACTION
static inline int performRandomTransaction() {
    struct timespec pause = {0};
    pause.tv_nsec = 10000000; // 10 ms (100*10^6 ns)
    nanosleep(&pause, NULL);
    //return 1; /** This permits to easily check the correctness of the exercise **/ 
    int amount = rand() % (2 * MAX_TRANSACTION); // {0, ..., 2*MAX_TRANSACTION - 1}
    if (amount >= MAX_TRANSACTION) {
        return MAX_TRANSACTION - (amount+1); // {-MAX_TRANSACTION, ..., -1}
    } else { 
        return amount + 1; // {1, ..., MAX_TRANSACTION}
    }
}

// producer thread
void* performTransactions(void* x) {
    thread_args_t* args = (thread_args_t*)x;
    printf("Starting producer thread %d\n", args->threadId);

    while (args->numOps > 0) {
        // produce the item, outside the critical section
        int currentTransaction = performRandomTransaction();
        
        /*
         * wait for at least one empty slot (e)
         * wait for your turn, mutex (s1)
         */
        if (sem_wait(&e)) handle_error("[producer] sem_wait error, sem e");      
        if (sem_wait(&s1)) handle_error("[producer] sem_wait error, sem s1");     

        // write the item and update write_index accordingly
        transactions[write_index] = currentTransaction;
        write_index = (write_index + 1) % BUFFER_SIZE;
        
        /*
         * I did my write! the next turn can go on (s1)
         * there is one filled slot! (n)
         */
        if (sem_post(&s1)) handle_error("[producer] sem_post error, sem s1");      
        if (sem_post(&n)) handle_error("[producer] sem_post error, sem n");      

        args->numOps--;
        //printf("P %d\n", args->numOps);
    }

    free(args);
    pthread_exit(NULL);
}

// consumer thread
void* processTransactions(void* x) {      
    thread_args_t* args = (thread_args_t*)x;
    printf("Starting consumer thread %d\n", args->threadId);

    while (args->numOps > 0) {
        
        /*
         * wait for at least one filled slot (n)
         * wait for your turn, mutex (s2)
         */
        if (sem_wait(&n)) handle_error("[consumer] sem_wait error, sem n");       
        if (sem_wait(&s2)) handle_error("[consumer] sem_wait error, sem s2");
        
        // consume the item and update (shared) variable deposit
        deposit += transactions[read_index];
        read_index = (read_index + 1) % BUFFER_SIZE;
        if (read_index % 100 == 0)
			printf("After the last 100 transactions balance is now %d.\n", deposit);
        
        /*
         * I did my read! the next turn can go on (s2)
         * there is one empty slot! (e)
         */
        if (sem_post(&s2)) handle_error("[consumer] sem_post error, sem s2");      
        if (sem_post(&e)) handle_error("[consumer] sem_post error, sem e");      
        
        args->numOps--;
        //printf("C %d\n", args->numOps);
    }

    free(args);
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    printf("Welcome! This program simulates financial transactions on a deposit.\n");
    printf("\nThe maximum amount of a single transaction is %d (negative or positive).\n", MAX_TRANSACTION);
    printf("\nInitial balance is %d. Press CTRL+C to quit.\n\n", INITIAL_DEPOSIT);

    // initialize read and write indexes
    read_index  = 0;
    write_index = 0;

    /* 
     * semaphores initialization:
     * n  = 0           (the buffer is empty, 0 items to consume)
     * e  = BUFFER_SIZE (all slots are empty and available for production)
     * s1 = 1           (producer mutex starts unlocked)
     * s2 = 1           (consumer mutex starts unlocked)
     */
    if (sem_init(&n, 0, 0)) handle_error("sem_init error, sem n");
    if (sem_init(&e, 0, BUFFER_SIZE)) handle_error("sem_init error, sem e");
    if (sem_init(&s1, 0, 1)) handle_error("sem_init error, sem s1");
    if (sem_init(&s2, 0, 1)) handle_error("sem_init error, sem s2");

    // set seed for pseudo-random number generator: we use this to make
    // this code yield the same result across different runs, as long
    // as they are race-free and you make no mistakes :-)
    srand(PRNG_SEED); 

    int ret;
    pthread_t producer[NUM_PRODUCERS], consumer[NUM_CONSUMERS];

    /* spawning producer threads */
    int i;
    for (i=0; i<NUM_PRODUCERS; ++i) {
        thread_args_t* arg = malloc(sizeof(thread_args_t));
        arg->threadId = i;
        arg->numOps = OPS_PER_PRODUCER;

        ret = pthread_create(&producer[i], NULL, performTransactions, arg);
        if (ret != 0) { fprintf(stderr, "Error %d in pthread_create\n", ret); exit(EXIT_FAILURE); }
    }

    /* spawning consumer threads */
    int j;
    for (j=0; j<NUM_CONSUMERS; ++j) {
        thread_args_t* arg = malloc(sizeof(thread_args_t));
        arg->threadId = j;
        arg->numOps = OPS_PER_CONSUMER;

        ret = pthread_create(&consumer[j], NULL, processTransactions, arg);
        if (ret != 0) { fprintf(stderr, "Error %d in pthread_create\n", ret); exit(EXIT_FAILURE); }
    }

    /* 
     * main thread waits for all producers and consumers to finish their operations
     * before continuing. This ensures we calculate the final deposit correctly.
     */
    for (i=0; i<NUM_PRODUCERS; ++i) {
        ret = pthread_join(producer[i], NULL);
        if (ret != 0) { fprintf(stderr, "Error %d in pthread_join\n", ret); exit(EXIT_FAILURE); }
    }

    for (j=0; j<NUM_CONSUMERS; ++j) {
        ret = pthread_join(consumer[j], NULL);
        if (ret != 0) { fprintf(stderr, "Error %d in pthread_join\n", ret); exit(EXIT_FAILURE); }
    }

    printf("Final value for deposit: %d\n", deposit);
    
    /* clean up os resources */
    if (sem_destroy(&n)) handle_error("sem_destroy error, sem n");
    if (sem_destroy(&e)) handle_error("sem_destroy error, sem e");
    if (sem_destroy(&s1)) handle_error("sem_destroy error, sem s1");
    if (sem_destroy(&s2)) handle_error("sem_destroy error, sem s2");

    exit(EXIT_SUCCESS);
}
