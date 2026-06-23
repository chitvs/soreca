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
 * Exercise 1 - Concurrent access to shared variables
 * 
 * The goal is to fix a race condition.
 * N threads are launched simultaneously writing to the same shared
 * variable, how should this be prevented?
 * 
 * by using a global array which contains separate variables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

/* some constants, which may be fine-tuned for testing different scenarios */
#define N 1000 // number of threads
#define M 10000 // number of iterations per thread
#define V 1 // value added to the balance by each thread at each iteration

/* 
 * The shared variable must be an array of different shared variables;
 * the idea is simple, memory addresses are accessed through indexes.
 * 
 * In the original code the problem is brute adding the values like this:
 * 
 * shared_variable += v;
 * 
 * if 3 cats are all eating from the same basket, they fight against each other!!!
 * to prevent this, 3 different baskets must be allocated.
*/

/* here comes the array of shared variables */
unsigned long int* shared;
int n = N, m = M, v = V;

/* threads work through indexing*/
void* thread_work(void *arg) {
    int thread_idx = *((int*)arg);
    int i; 
	for (i = 0; i < m; i++)
		shared[thread_idx] += v; /* the value is added in the right place */
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc > 1) n = atoi(argv[1]);
	if (argc > 2) m = atoi(argv[2]);
	if (argc > 3) v = atoi(argv[3]);

    /* calloc is used instead of malloc, so the memory is both allocated and initialized to zero */
	shared = (unsigned long int*)calloc(n, sizeof(unsigned long int));

	printf("Going to start %d threads, each adding %d times %d to a shared variable initialized to zero...", n, m, v); fflush(stdout);
	pthread_t* threads = (pthread_t*)malloc(n * sizeof(pthread_t)); // also calloc(n,sizeof(pthread_t))

    /* array of thread ids to safely pass each one */
	int* thread_ids = (int*)malloc(n * sizeof(int));

    int i;
	for (i = 0; i < n; i++){
        thread_ids[i] = i;
		if (pthread_create(&threads[i], NULL, thread_work, &thread_ids[i]) != 0) {
			fprintf(stderr, "Can't create a new thread, error %d\n", errno);
			exit(EXIT_FAILURE);
		}
    }
	printf("ok\n");

	printf("Waiting for the termination of all the %d threads...", n); fflush(stdout);

    /* 
     * pthread_join forces the main thread to wait for each child to finish its iterations. 
     * Then its partial result is collected into the final value.
     */
    unsigned long int value = 0;
	for (i = 0; i < n; i++){
		pthread_join(threads[i], NULL);
        value += shared[i];
    }
	printf("ok\n");

	unsigned long int expected_value = (unsigned long int)n*m*v;
	printf("The value of the shared variable is %lu. It should have been %lu\n", value, expected_value);
	if (expected_value > value) {
		unsigned long int lost_adds = (expected_value - value) / v;
		printf("Number of lost adds: %lu\n", lost_adds);
	}
    free(shared);
    free(threads);
    free(thread_ids);

	return EXIT_SUCCESS;
}
