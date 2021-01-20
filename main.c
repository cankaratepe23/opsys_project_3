#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>

/*
 * NOTES:
 * A Book struct is needed
 * How do we check if all publisher threads have exited?
 * Do we need a struct for publisher threads?
 */

int publisher_type, publisher_thread_count, packager_thread_count, book_per_thread, package_size, buffer_size;

// Publisher struct to hold information about publisher types, such as the id, or the current buffer size and location.
typedef struct publisher {
    int id;
    void **buffer;
    int buflen;
    sem_t full;
    sem_t empty;
    pthread_mutex_t lock;
    int in;
    int out;
    int totalBookCount;
} Publisher;

// Initialize a single publisher type with the current global variables.
void init_buffer(Publisher *publisher) {
    printf("DEBUG: Allocating memory for buffer %d\n", publisher->id);
    publisher->buflen = buffer_size;
    publisher->buffer = calloc(0, sizeof(void *) * publisher->buflen);
    printf("DEBUG: Initializing in/out values and book count for buffer %d", publisher->id);
    publisher->in = 0;
    publisher->out = 0;
    publisher->totalBookCount = 0;
    printf("DEBUG: Initializing fullsem for buffer %d.\n", publisher->id);
    sem_init(&publisher->full, 0, 0);
    printf("DEBUG: Initializing emptysem for buffer %d.\n", publisher->id);
    sem_init(&publisher->empty, 0, publisher->buflen);
    printf("DEBUG: Initializing mutex locks for buffer %d.\n", publisher->id);
    pthread_mutex_init(&publisher->lock, NULL);
}

// Create and initialize all publisher types
void init(Publisher ** out) {
    for (int i = 0; i < publisher_type; ++i) {
        printf("DEBUG: Initializing values for publisher type %d\n", i);
        Publisher *publisher = malloc(sizeof(Publisher));
        publisher->id = i;
        init_buffer(publisher);
        out[i] = publisher;
    }
    printf("DEBUG: Init done.\n");
}

// Double the size of the buffer of the given publisher, update the semaphore accordingly.
void resizebuffer(Publisher *publisher, int oldsize) {
    // We first copy the current buffer's address to a temporary variable, so that it's not lost.
    int **tempbuffer = (int **)publisher->buffer;
    // Allocate new memory for the buffer and set the buflen variable to the new size.
    publisher->buffer = calloc(0, (sizeof (void *)) * (oldsize * 2));
    publisher->buflen = oldsize * 2;
    // Re-initialize the 'empty' semaphore with the number of newly created empty slots.
    sem_destroy(&publisher->empty);
    sem_init(&publisher->empty, 0, oldsize);
    // TODO Implement a buffercpy method here?
    // Copy the contents of the old buffer to the newly allocated one.
    for (int i = 0; i < oldsize; ++i) {
        publisher->buffer[i] = tempbuffer[i];
    }
}

void enqueue(Publisher *publisher, void *value) {
    printf("DEBUG: Waiting for empty spaces (if needed)\n");
    int semval = 0;
    sem_getvalue(&publisher->empty, &semval);
    printf("DEBUG: Semaphore value before trywait(): %d\n", semval);
    // Try to wait for the empty semaphore, which effectively means we check if there are any empty slots in the buffer.
    int semtrywaitsuccess = sem_trywait(&publisher->empty);
    sem_getvalue(&publisher->empty, &semval);
    printf("DEBUG: Semaphore value after trywait(): %d\n", semval);
    if (semtrywaitsuccess == -1 && errno == EAGAIN) { // sem_trywait() will return -1 and errno will be set to EAGAIN if semaphore is 0
        printf("DEBUG: Buffer is full. Resizing...\n");
        resizebuffer(publisher, publisher->buflen);
        sem_getvalue(&publisher->empty, &semval);
        printf("DEBUG: Semaphore value after resize: %d\n", semval);
    }
    printf("DEBUG: Locking mutex for enqueue\n");
    // Lock the mutex for the buffer before writing to it.
    pthread_mutex_lock(&publisher->lock);
    int bufferwriteindex = (publisher->in++) % publisher->buflen;
    if (bufferwriteindex < 0) {
        fprintf(stderr, "ERROR: Integer was already overflowed when trying to queue value to buffer %d\n", publisher->id);
        exit(-1);
    }
    printf("DEBUG: Writing value %d to buffer %d position %d\n", *(int *)value, publisher->id, bufferwriteindex);
    publisher->buffer[bufferwriteindex] = value;
    publisher->totalBookCount++;
    printf("DEBUG: Unlocking mutex after enqueue\n");
    // Unlock the mutex after writing to the buffer is done.
    pthread_mutex_unlock(&publisher->lock);
    printf("DEBUG: Updating semaphore\n");
    // Update the full semaphore value to reflect the number of full slots in the buffer, or the number of books in the buffer
    sem_post(&publisher->full);
}

void *dequeue(Publisher *publisher) {
    printf("DEBUG: Waiting for items in buffer (if needed)\n");
    sem_wait(&publisher->full); // Waits if there are no items left in the buffer.
    printf("DEBUG: Locking mutex for dequeue\n");
    pthread_mutex_lock(&publisher->lock);
    int bufferreadindex = (publisher->out++) % publisher->buflen;
    if (bufferreadindex < 0) {
        printf("Integer was already overflowed when trying to queue value to buffer %d\n", publisher->id);
        exit(-1);
    }
    printf("DEBUG: Reading value from buffer %d at position %d\n", publisher->id, bufferreadindex);
    void *result = publisher->buffer[bufferreadindex];
    printf("DEBUG: Unlocking mutex after dequeue\n");
    pthread_mutex_unlock(&publisher->lock);
    printf("DEBUG: Updating semaphore\n");
    sem_post(&publisher->empty);
    return result;
}

void publish(Publisher *publisher) {
    // TODO Implement
}

int main(int argc, char *argv[]) {
    if (argc != 10) {
        fprintf(stderr,
                "Usage: project3.out {-n publisher_type publisher_thread_count packager_thread_count} {-b books_per_thread} {-s package_size buffer_size}");
        exit(-1);
    }
    int argcounter = 0;
    while (++argcounter < 9) {
        if (strcmp(argv[argcounter], "-n") == 0) {
            publisher_type = (int) strtol(argv[++argcounter], NULL, 10);
            publisher_thread_count = (int) strtol(argv[++argcounter], NULL, 10);
            packager_thread_count = (int) strtol(argv[++argcounter], NULL, 10);
        }
        else if (strcmp(argv[argcounter], "-b") == 0) {
            book_per_thread = (int) strtol(argv[++argcounter], NULL, 10);
        }
        else if (strcmp(argv[argcounter], "-s") == 0) {
            package_size = (int) strtol(argv[++argcounter], NULL, 10);
            buffer_size = (int) strtol(argv[++argcounter], NULL, 10);
        }
        else {
            fprintf(stderr,
                    "Usage: project3.out {-n publisher_type publisher_thread_count packager_thread_count} {-b books_per_thread} {-s package_size buffer_size}");
            exit(-1);
        }
    }
    Publisher *publishers[publisher_type];
    init(publishers);

    int val1 = 10;
    int val2 = 20;
    int val3 = 30;
    int val4 = 40;
    int val5 = 50;
    int val6 = 60;
    int val7 = 70;
    int val8 = 80;
    int val9 = 90;
    int val10 = 100;
    int val11 = 110;
    enqueue(publishers[0], &val1);
    enqueue(publishers[0], &val2);
    enqueue(publishers[0], &val3);
    enqueue(publishers[0], &val4);
    enqueue(publishers[0], &val5);
    enqueue(publishers[0], &val6);
    enqueue(publishers[0], &val7);
    enqueue(publishers[0], &val8);
    enqueue(publishers[0], &val9);
    enqueue(publishers[0], &val10);
    enqueue(publishers[0], &val11);

    printf("Execution complete.\n");
    fprintf(stderr, "Error test.\n");
    return 0;
}
