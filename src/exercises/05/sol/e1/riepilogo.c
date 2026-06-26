/*
 * This is part of the fifth session.
 *
 * Goals:
 * - Summary exercise on processes, threads, semaphores, and shared memory.
 *
 * Exercise 1 - Synchronization summary
 * 
 * The application implements the following semantics:
 * 
 * 1. The main process creates N child processes via fork().
 * 
 * 2. All child processes synchronize to start their activity, triggered by the main process.
 * 
 * 3. The activity of child processes consists of launching M threads at a time.
 *  a. Threads compete for access to a critical section.
 *  b. Once in the critical section, they append the child process identity to a file.
 * 
 * 4. After T seconds, the main process notifies the child processes to cease their 
 * activity and terminate (using a shared memory flag).
 *  a. Before terminating, a process must wait for the end of currently executing threads.
 * 
 * 5. Finally, the main process identifies the process that accessed the critical section the most.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* Macros for error handling */
#include "common.h"

#define N 100 /* child process count */
#define M 10  /* thread per child process count */
#define T 3   /* time to sleep for main process */

#define SHM_NAME "/shmem-notification"
#define MAIN_WAITS_FOR_CHILDREN_SEMAPHORE_NAME "/mainWait"
#define CHILDREN_WAIT_FOR_MAIN_SEMAPHORE_NAME "/childrenWait"
#define CRITICAL_SECTION "/criticalSection"

#define FILENAME "accesses.log"

/* Data structure required by threads */
typedef struct thread_args_s {
    unsigned int child_id;
    unsigned int thread_id;
} thread_args_t;

/* Parameters can be set also via command-line arguments */
int n = N, m = M, t = T;

sem_t *mainWait = NULL;
sem_t *childrenWait = NULL;
sem_t *criticalSection = NULL;

int shm_fd;
int *data;

/*
 * Initialize file
 *
 * Ensures that an empty file with the given name exists.
 */
void init_file(const char *filename) {
    printf("[Main] Initializing file %s...", FILENAME);
    fflush(stdout);
    
    int fd = open(FILENAME, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) handle_error("error while initializing file");
    
    close(fd);
    printf("closed...file correctly initialized!!!\n");
}

/*
 * Create named semaphore
 *
 * Creates a named semaphore with a given name, mode, and initial value.
 * Also tries to remove any pre-existing semaphore with the same name.
 */
sem_t *create_named_semaphore(const char *name, mode_t mode, unsigned int value) {
    printf("[Main] Creating named semaphore %s...", name);
    fflush(stdout);
    
    /* If it exists, unlink it first to start fresh */
    sem_unlink(name);
    sem_t *sem = sem_open(name, O_CREAT | O_EXCL, mode, value);
    if (sem == SEM_FAILED) {
        handle_error("Error creating named semaphore");
    }
    
    printf("done!!!\n");
    return sem;
}

/*
 * Parse output
 *
 * Identifies the child that accessed the file the most times.
 */
void parseOutput() {
    int* access_stats = calloc(n, sizeof(int)); /* initialized with zeros */
    
    printf("[Main] Opening file %s in read-only mode...", FILENAME);
    fflush(stdout);
    
    int fd = open(FILENAME, O_RDONLY);
    if (fd < 0) handle_error("error while opening output file");
    
    printf("ok, reading it and updating access stats...");
    fflush(stdout);
    
    size_t read_bytes;
    int index;
    do {
        read_bytes = read(fd, &index, sizeof(int));
        if (read_bytes > 0)
            access_stats[index]++;
    } while(read_bytes > 0);
    
    printf("ok, closing it...");
    fflush(stdout);
    
    close(fd);
    printf("closed!!!\n");
    
    int max_child_id = -1, max_accesses = -1;
    for (int i = 0; i < n; i++) {
        printf("[Main] Child %d accessed file %s %d times\n", i, FILENAME, access_stats[i]);
        if (access_stats[i] > max_accesses) {
            max_accesses = access_stats[i];
            max_child_id = i;
        }
    }
    printf("[Main] ===> The process that accessed the file most often is %d (%d accesses)\n", max_child_id, max_accesses);
    
    free(access_stats);
}

void* thread_function(void* x) {
    thread_args_t *args = (thread_args_t*)x;
    
    /* Enter critical section */    
    int ret = sem_wait(criticalSection);
    if(ret) handle_error("sem_wait failed");
    
    printf("[Child#%d-Thread#%d] Entered into critical section!!!\n", args->child_id, args->thread_id);
    
    int fd = open(FILENAME, O_WRONLY | O_APPEND);
    if (fd < 0) handle_error("error while opening file");
    printf("[Child#%d-Thread#%d] File %s opened in append mode!!!\n", args->child_id, args->thread_id, FILENAME);   
    
    write(fd, &(args->child_id), sizeof(int));
    printf("[Child#%d-Thread#%d] %d appended to file %s opened in append mode!!!\n", args->child_id, args->thread_id, args->child_id, FILENAME);    
    
    close(fd);
    printf("[Child#%d-Thread#%d] File %s closed!!!\n", args->child_id, args->thread_id, FILENAME);
    
    /* Exit critical section */
    ret = sem_post(criticalSection);
    if(ret) handle_error("sem_post failed");
    
    printf("[Child#%d-Thread#%d] Exited from critical section!!!\n", args->child_id, args->thread_id);
    
    free(x);
    pthread_exit(NULL);
}

void mainProcess() {
    /*
     * The main process waits for all the children to start,
     * it notifies them to start their activities, and sleeps
     * for some time t. Once it wakes up, it notifies the children
     * to end their activities, and waits for their termination.
     * Finally, it calls the parseOutput() method and releases
     * any shared resources. 
     */
    printf("[Main] %d children created, wait for all children to be ready...\n", n);
    int i, ret;
    for (i = 0; i < n; i++) {
        ret = sem_wait(mainWait);
        if(ret) handle_error("sem_wait failed");
    }
    printf("[Main] All the children are now ready!!!\n");
    
    /* Notify children to start their activities */
    printf("[Main] Notifying children to start their activities...\n");
    for (i = 0; i < n; i++) {
        ret = sem_post(childrenWait);
        if(ret) handle_error("sem_post failed");
    }
    printf("[Main] Children have been notified to start their activities!!!\n");
    
    /* Main process sleeps */
    printf("[Main] Sleeping for %d seconds...\n", t);
    sleep(t);
    printf("[Main] Woke up after having slept for %d seconds!!!\n", t);
    
    /* Notify children to end their activities */
    printf("[Main] Notifying children to end their activities...\n");
    *data = 1;
    printf("[Main] Children have been notified to end their activities!!!\n");
    
    /* Wait for all the children to terminate */
    printf("[Main] Waiting for all the children to terminate...\n");
    int child_status;
    for (i = 0; i < n; i++) {
        ret = wait(&child_status);
        if(ret == -1) handle_error("wait failed");
        
        if (WEXITSTATUS(child_status)) {
            fprintf(stderr, "ERROR: child died with code %d\n", WEXITSTATUS(child_status));
            exit(EXIT_FAILURE);
        }
    }
    printf("[Main] All the children have terminated!!!\n");
    
    /* Identify the child that accessed the file most times */
    parseOutput();
    
    /* Global clean up */
    printf("[Main] Cleaning up...");
    fflush(stdout);
    
    ret = close(shm_fd);
    if (ret == -1) handle_error("main: cannot close the shared memory");
    
    if (munmap(data, sizeof(int)) == -1) handle_error("main unmap");
    
    shm_unlink(SHM_NAME);
    
    ret = sem_close(mainWait);
    if(ret) handle_error("sem_close failed");
    
    ret = sem_unlink(MAIN_WAITS_FOR_CHILDREN_SEMAPHORE_NAME);
    if(ret) handle_error("sem_unlink failed");
    
    ret = sem_close(childrenWait);
    if(ret) handle_error("sem_close failed");
    
    ret = sem_unlink(CHILDREN_WAIT_FOR_MAIN_SEMAPHORE_NAME);
    if(ret) handle_error("sem_unlink failed");
    
    ret = sem_close(criticalSection);
    if(ret) handle_error("sem_close failed");
    
    ret = sem_unlink(CRITICAL_SECTION);
    if(ret) handle_error("sem_unlink failed");
    
    printf("done!!!\n");
}

void childProcess(int child_id) {
    /*
     * Each child process notifies the main process that it
     * is ready, then waits to be notified from the main in order
     * to start. As long as the main process does not notify a
     * termination event, the child process repeatedly creates m threads 
     * that execute function thread_function() and waits for their completion. 
     * When a notification has arrived, the child process releases any
     * shared resources before exiting. 
     */
    printf("[Child#%d] Child process initialized\n", child_id);
    
    /* Notify main process that this child is ready */
    int ret = sem_post(mainWait);
    if(ret) handle_error("sem_post_failed");
    
    printf("[Child#%d] Main process notified that I am ready!!!\n", child_id);
    
    /* Wait for main to notify this child to begin activities */
    ret = sem_wait(childrenWait);
    if(ret) handle_error("sem_wait failed");
    
    printf("[Child#%d] Notification to begin received!!!\n", child_id);
    
    unsigned int thread_id = 0;
    pthread_t* thread_handlers = malloc(m * sizeof(pthread_t));
    
    do {
        int j;
        
        /* Reuse the buffer across iterations */
        memset(thread_handlers, 0, m * sizeof(pthread_t));
        
        /* Create M threads */
        printf("[Child#%d] Creating %d threads...\n", child_id, m);
        for (j = 0; j < m; j++) {
            thread_args_t *t_args = (thread_args_t *)malloc(sizeof(thread_args_t));
            t_args->child_id = child_id;
            t_args->thread_id = thread_id++;
            
            ret = pthread_create(&thread_handlers[j], NULL, thread_function, t_args);
            if(ret) handle_error_en(ret, "pthread_create failed");
        }
        printf("[Child#%d] %d threads created!!!\n", child_id, m);
        
        /* Wait for their completion */
        printf("[Child#%d] Waiting for the end of the %d threads...\n", child_id, m);
        for (j = 0; j < m; j++) {
            ret = pthread_join(thread_handlers[j], NULL);
            if(ret) handle_error_en(ret, "pthread_join failed");
        }
        printf("[Child#%d] %d threads completed!!!\n", child_id, m);
        
        printf("[Child#%d] Checking for end activities notification...\n", child_id);
        
        /* Check the shared memory flag */
        if (*data) break;
        
        printf("[Child#%d] Go on with activities!!!\n", child_id);
    } while(1);
    
    free(thread_handlers);
    
    printf("[Child#%d] Activities completed!!!\n", child_id);
    
    /* Local cleanup */
    if (munmap(data, sizeof(int)) == -1) handle_error("main unmap");
    
    ret = close(shm_fd);
    if (ret == -1) handle_error("main: cannot close the shared memory");
    
    ret = sem_close(mainWait);
    if(ret) handle_error("sem_close failed");
    
    ret = sem_close(childrenWait);
    if(ret) handle_error("sem_close failed");
    
    ret = sem_close(criticalSection);
    if(ret) handle_error("sem_close failed");
}

int main(int argc, char **argv) {
    /* Arguments parsing */
    if (argc > 1) n = atoi(argv[1]);
    if (argc > 2) m = atoi(argv[2]);
    if (argc > 3) t = atoi(argv[3]);
    
    /* Initialize the file */
    init_file(FILENAME);
    
    /* Shared memory initialization */
    shm_unlink(SHM_NAME);
    
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd < 0) handle_error("main: error in shm_open");
    
    if (ftruncate(shm_fd, sizeof(int)) == -1) handle_error ("main: ftruncate");
    
    if ((data = (int *) mmap(0, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        handle_error ("main: mmap");
        
    printf("[Main] Shared memory mapped successfully at address: %p\n", data);
    *data = 0;
    
    /* Named semaphores creation */
    mainWait = create_named_semaphore(MAIN_WAITS_FOR_CHILDREN_SEMAPHORE_NAME, 0600, 0);
    childrenWait = create_named_semaphore(CHILDREN_WAIT_FOR_MAIN_SEMAPHORE_NAME, 0600, 0);
    criticalSection = create_named_semaphore(CRITICAL_SECTION, 0600, 1);
    
    /* Create the N children */
    printf("[Main] Creating %d children...\n", n);
    int i;
    for (i = 0; i < n; i++) {
        pid_t pid = fork();
        
        if (pid == -1) {
            printf("Error creating child process #%d: %s\n", i, strerror(errno));
            /* Note: any other process forked so far must be killed manually upon failure */
            exit(EXIT_FAILURE); 
        } else if (pid == 0) {
            /* Child process: execute logic and exit */
            printf("[Child#%d] Child process created, pid %d\n", i, getpid());
            childProcess(i);
            _exit(EXIT_SUCCESS);
        }
    }
    
    /* Main process executes its specific logic */
    mainProcess();
    
    exit(EXIT_SUCCESS);
}
