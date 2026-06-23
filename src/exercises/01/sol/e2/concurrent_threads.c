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
 * Exercise 2 - Mutual exclusion for critical section access
 * 
 * The goal is to fix a race condition.
 * N threads are launched simultaneously writing to the same shared
 * variable, how should this be prevented?
 * 
 * by enforcing mutual exclusion using a binary semaphore (mutex) 
 * to protect the critical section.
 */

#include "../performance.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>

/* some constants, which may be fine-tuned for testing different scenarios */
#define N 1000 // number of threads
#define M 10000 // number of iterations per thread
#define V 1 // value added to the balance by each thread at each iteration

/* 
 * unlike exercise 1, here there is only one shared variable (one basket).
 * If 3 cats want to eat from the same basket, they must wait in line.
 * To prevent them from fighting, a semaphore is used as a lock
 * to ensure that only one thread at a time can access the critical section.
 */
sem_t sem;
unsigned long int shared_variable;
int n = N, m = M, v = V;

/* threads perform operations safely inside the critical section */
void* thread_work(void *arg) {
	int i;
	for (i = 0; i < m; i++){

        /* 
         * sem_wait is the entry door. 
         * It decrements the semaphore. If the value becomes negative (or was already 0), 
         * the thread is put to sleep in a queue. If positive, the thread enters.
         */
		if (sem_wait(&sem) != 0) {
			fprintf(stderr, "acquisition error\n");
			return NULL;
		}
		
        /* critical section */
		shared_variable += v;
		
        /* 
         * sem_post is the exit door. 
         * It increments the semaphore. If there are threads sleeping in the queue, 
         * one of them is woken up and allowed to enter the critical section.
         */
		if (sem_post(&sem) != 0) {
			fprintf(stderr, "release error\n");
			return NULL;
		}
		
	}
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc > 1) n = atoi(argv[1]);
	if (argc > 2) m = atoi(argv[2]);
	if (argc > 3) v = atoi(argv[3]);
	shared_variable = 0;

    /* timer struct used to evaluate the semaphore overhead */
	timer t;
	
	printf("Going to start %d threads, each adding %d times %d to a shared variable initialized to zero...", n, m, v); fflush(stdout);
	pthread_t* threads = (pthread_t*)malloc(n * sizeof(pthread_t));
	int i;
	
    /* 
     * sem_init initializes the semaphore.
     * The second parameter (0) specifies that the semaphore is shared only among 
     * threads of the current process. 
     * The third parameter (1) is the initial value: 1 means the lock is open (binary semaphore).
     */
	if (sem_init(&sem, 0, 1) != 0) {
		fprintf(stderr, "init error\n");
		exit(EXIT_FAILURE);
	}
	
	begin(&t);
	
    /* starting threads. NULL is passed as argument since all threads share the same global variable */
	for (i = 0; i < n; i++)
		if (pthread_create(&threads[i], NULL, thread_work, NULL) != 0) {
			fprintf(stderr, "Can't create a new thread, error %d\n", errno);
			exit(EXIT_FAILURE);
		}
	printf("ok\n");
	
	printf("Waiting for the termination of all the %d threads...", n); fflush(stdout);
	for (i = 0; i < n; i++)
		pthread_join(threads[i], NULL);
	end(&t);
	printf("ok\n");
	
	unsigned long int expected_value = (unsigned long int)n*m*v;
	printf("The value of the shared variable is %lu. It should have been %lu\n", shared_variable, expected_value);
	if (expected_value > shared_variable) {
		unsigned long int lost_adds = (expected_value - shared_variable) / v;
		printf("Number of lost adds: %lu\n", lost_adds);
	}
	
    /* 
     * Calculating the execution time shows the semaphore overhead.
     * Calling wait and post millions of times forces the OS scheduler to intervene 
     * constantly, making this approach much slower than the lock-free array of exercise 1.
     */
	printf("Time: %lu ms\n", get_milliseconds(&t));
	free(threads);
	sem_destroy(&sem); /* always destroy the semaphore to free system resources */
	return EXIT_SUCCESS;
}
