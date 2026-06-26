/*
 * This is part of the fourth session.
 *
 * Goals:
 * - learn to perform input and output operations using descriptors (fd) in UNIX
 * - read/write to files
 * - implement IPC (Inter-Process Communication) using anonymous pipes
 * - implement IPC between unrelated processes using named pipes (FIFOs)
 * - send/receive messages on sockets (future labs)
 *
 * Exercise 2 - Unidirectional IPC via pipe with synchronization
 *
 * The goal is to establish a unidirectional communication channel among related
 * processes (parent and children) using a single anonymous pipe.
 *
 * How is the execution coordinated?
 *
 * - The parent creates a single anonymous pipe and named semaphores.
 * - The parent spawns WRITERS_COUNT writer processes and READERS_COUNT reader processes.
 * - Writers write messages to the pipe in mutual exclusion using WRITE_MUTEX.
 * - Readers read from the pipe in mutual exclusion using READ_MUTEX.
 *
 * Critical aspects handled in this implementation:
 * - Pipe descriptors management: each child must close the unused end of the pipe
 * (readers close the write end, writers close the read end).
 * - Partial I/O and Interrupts: read() and write() operations are looped to ensure
 * the exact amount of bytes is transferred, retrying on EINTR.
 * - Message integrity: readers verify that the received array contains consistent values.
 */

#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/wait.h>
#include "common.h"

#define WRITERS_COUNT 2
#define READERS_COUNT 3
#define WRITE_MUTEX "/write_mutex"
#define READ_MUTEX "/read_mutex"
#define MSG_COUNT 12
#define MSG_ELEMS (64 * PIPE_BUF)

int pipefd[2];

/*
 * Write to pipe
 *
 * Writes exactly 'data_len' bytes to the specified file descriptor.
 * Handles partial writes and interrupts (EINTR).
 */
int write_to_pipe(int fd, const void *data, size_t data_len) {
    int written_bytes = 0, ret;

    while (written_bytes < data_len) {
        ret = write(fd, data + written_bytes, data_len - written_bytes);

        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("write error");

        written_bytes += ret;
    }
    return written_bytes;
}

/*
 * Read from pipe
 *
 * Reads exactly 'data_len' bytes from the specified file descriptor.
 * Handles partial reads and interrupts (EINTR). Aborts on unexpected EOF.
 */
int read_from_pipe(int fd, void *data, size_t data_len) {
    int read_bytes = 0, ret;

    while (read_bytes < data_len) {
        ret = read(fd, data + read_bytes, data_len - read_bytes);

        if (ret == -1 && errno == EINTR) continue;
        if (ret == -1) handle_error("read error");

        /* If read returns 0 before fulfilling data_len, the pipe was closed unexpectedly */
        if (ret == 0) handle_error("close error");

        read_bytes += ret;
    }
    return read_bytes;
}

/* 
 * Fills the 'data' array with 'elem_count' copies of 'value' 
 */
void create_msg(int *data, int elem_count, int value) {
    int i;
    for (i = 0; i < elem_count; i++) {
        data[i] = value;
    }
}

/* 
 * Verifies that all elements in the 'data' array have the same value.
 * Returns 1 if valid, 0 if corrupted.
 */
int is_msg_ok(const int *data, int elem_count) {
    int i;
    for (i = 0; i < elem_count; i++)
        if (data[0] != data[i])
            return 0;
    return 1;
}

void reader(int reader_id, sem_t *read_mutex) {
    int data[MSG_ELEMS];
    int i, ret;

    printf("[READER_%d] processo reader creato.\n", reader_id);

    /* 
     * Close the unused write end of the pipe.
     * This is mandatory to allow EOF detection if all writers terminate.
     */
    ret = close(pipefd[1]);
    if (ret) handle_error("close error");

    for (i = 0; i < MSG_COUNT / READERS_COUNT; i++) {

        /* Acquire read mutex to ensure atomic message reading */
        ret = sem_wait(read_mutex);
        if (ret) handle_error("error waiting on read mutex");

        /* Size is evaluated exactly in bytes: MSG_ELEMS * sizeof(int) */
        read_from_pipe(pipefd[0], data, sizeof(data)); // sizeof(data) == MSG_ELEMS * sizeof(int)

        /* Release read mutex */
        ret = sem_post(read_mutex);
        if (ret) handle_error("error posting on read mutex");

        printf("[CHILD_%d] Letto msg #%d con valore %d\n", reader_id, i, data[0]);
        if (!is_msg_ok(data, MSG_ELEMS))
            printf("corrupted message!!!\n");
    }

    /* Cleanup local semaphore descriptor */
    ret = sem_close(read_mutex);
    if (ret)
        handle_error("error closing read mutex");

    /* Close the remaining pipe descriptor before exiting */
    ret = close(pipefd[0]);
    if (ret) handle_error("error closing pipe");
}

void writer(int writer_id, sem_t *write_mutex) {
    int data[MSG_ELEMS];
    int i, ret;

    printf("[WRITER_%d] processo writer creato.\n", writer_id);

    /* Close the unused read end of the pipe */
    ret = close(pipefd[0]);
    if (ret) handle_error("close error");

    for (i = 0; i < MSG_COUNT / WRITERS_COUNT; i++) {
        create_msg(data, MSG_ELEMS, i);

        /* Acquire write mutex to ensure atomic message writing */
        ret = sem_wait(write_mutex);
        if (ret) handle_error("error waiting on write mutex");

        /* Size is evaluated exactly in bytes: MSG_ELEMS * sizeof(int) */
        write_to_pipe(pipefd[1], data, sizeof(data)); // sizeof(data) == MSG_ELEMS * sizeof(int)

        /* Release write mutex */
        ret = sem_post(write_mutex);
        if (ret) handle_error("error posting on write mutex");

        printf("[WRITER_%d] Inviato il msg #%d\n", writer_id, i);
    }

    /* Cleanup local semaphore descriptor */
    ret = sem_close(write_mutex);
    if (ret) handle_error("error closing write mutex");

    /* Close the remaining pipe descriptor before exiting */
    ret = close(pipefd[1]);
    if (ret) handle_error("close error");
}

int main(int argc, char *argv[]) {
    int ret, i;
    pid_t pid;

    /*
     * Preventive unlink and creation of named semaphores 
     * Initialized to 1 since they act as mutexes.
     */
    sem_unlink(READ_MUTEX);
    sem_t *read_mutex = sem_open(READ_MUTEX, O_CREAT | O_EXCL, 0600, 1);
    if (read_mutex == SEM_FAILED) handle_error("Error Creating Read Mutex");

    sem_unlink(WRITE_MUTEX);
    sem_t *write_mutex = sem_open(WRITE_MUTEX, O_CREAT | O_EXCL, 0600, 1);
    if (write_mutex == SEM_FAILED) handle_error("Error Creating Write Mutex");

    /* Create the anonymous pipe */
    ret = pipe(pipefd);
    if (ret) handle_error("pipe error");

    /* Spawn reader processes */
    for (i = 0; i < READERS_COUNT; i++) {
        pid = fork();
        if (pid == -1) handle_error("Error creating reader");
        if (pid == 0) {
            reader(i, read_mutex);
            _exit(0);
        }
    }

    /* Close local read mutex descriptor in the parent process */
    ret = sem_close(read_mutex);
    if (ret) handle_error("Error closing read mutex");

    /* Spawn writer processes */
    for (i = 0; i < WRITERS_COUNT; i++) {
        pid = fork();
        if (pid == -1) handle_error("error creating reader");
        if (pid == 0) {
            writer(i, write_mutex);
            _exit(0);
        }
    }

    /* Close local write mutex descriptor in the parent process */
    ret = sem_close(write_mutex);
    if (ret) handle_error("Error closing write mutex");

    /* 
     * Shutdown phase 
     *
     * Wait for all spawned processes (readers and writers) to terminate.
     */
    for (i = 0; i < READERS_COUNT + WRITERS_COUNT; i++) {
        int status;
        ret = wait(&status);
        if (ret == 1)
            handle_error("error waiting for a child to terminate");
        if (WEXITSTATUS(status))
            handle_error("child process died unexpectedly");
    }

    printf("[PARENT] processi figlio terminati.\n");

    /* Global cleanup (unlink the semaphores from the OS) */
    ret = sem_unlink(READ_MUTEX);
    if (ret) handle_error("error removing read mutex");

    ret = sem_unlink(WRITE_MUTEX);
    if (ret) handle_error("error removing write mutex");

    return 0;
}
