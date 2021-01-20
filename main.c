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
    pthread_t *threads;
    int rear;
    int front;
    int total_book_count;
} Publisher;

typedef struct book {
    int bookid;
    int typeid;
} Book;

typedef struct publisherargs {
    int publisherid;
    Publisher *publisher;

} PublisherArgs;

typedef struct packagerargs {
    int packagerid;
    Publisher **publishers;
} PackagerArgs;

void init_book(Publisher *publisher, Book **out) {
    (*out) = malloc(sizeof(Book));
    (*out)->typeid = publisher->id;
}

// Initialize a single publisher type with the current global variables.
void init_buffer(Publisher *publisher) {
    printf("DEBUG: Allocating memory for buffer %d\n", publisher->id);
    publisher->buflen = buffer_size;
    publisher->buffer = calloc(publisher->buflen, sizeof(Book *));
    publisher->threads = calloc(publisher_thread_count, sizeof(pthread_t));
    printf("DEBUG: Initializing rear/front values and book count for buffer %d", publisher->id);
    publisher->rear = 0;
    publisher->front = 0;
    publisher->total_book_count = 0;
    printf("DEBUG: Initializing fullsem for buffer %d.\n", publisher->id);
    sem_init(&publisher->full, 0, 0);
    printf("DEBUG: Initializing emptysem for buffer %d.\n", publisher->id);
    sem_init(&publisher->empty, 0, publisher->buflen);
    printf("DEBUG: Initializing mutex locks for buffer %d.\n", publisher->id);
    pthread_mutex_init(&publisher->lock, NULL);
}

// Create and initialize all publisher types
void init(Publisher **out) {
    for (int i = 0; i < publisher_type; ++i) {
        printf("DEBUG: Initializing values for publisher type %d\n", i);
        Publisher *publisher = malloc(sizeof(Publisher));
        publisher->id = i + 1;
        init_buffer(publisher);
        out[i] = publisher;
    }
    printf("DEBUG: Init done.\n");
}

// Double the size of the buffer of the given publisher, update the semaphore accordingly.
void resizebuffer(Publisher *publisher, int oldsize) {
    // We first copy the current buffer's address to a temporary variable, so that it's not lost.
    int **tempbuffer = (int **)publisher->buffer;
    int oldrear = publisher->rear;
    int oldfront = publisher->front;
    // Allocate new memory for the buffer and set the buflen variable to the new size.
    publisher->buffer = calloc((oldsize * 2), (sizeof (Book **)));
    publisher->buflen = oldsize * 2;
    // Re-initialize the 'empty' semaphore with the number of newly created empty slots.
    sem_destroy(&publisher->empty);
    sem_init(&publisher->empty, 0, oldsize);
    // TODO Implement a buffercpy method here?
    // Copy the contents of the old buffer to the newly allocated one.
    for (int i = 0; i <= oldrear; i++) {
        // Copy from the front of the old buffer until the rear is reached.
        publisher->buffer[i] = tempbuffer[i];
    }
    if (oldfront > oldrear) {
        // The list goes over the 'seam' of the circular buffer; we need to copy from the end of the old buffer as well.
        int newfront = publisher->buflen - (oldsize - oldfront);
        int old_buffer_index = oldfront;
        int new_buffer_index = newfront;
        while (old_buffer_index < oldsize) {
            publisher->buffer[new_buffer_index] = tempbuffer[old_buffer_index];
            old_buffer_index++;
            new_buffer_index++;
        }
        publisher->front = newfront;
    }
}

void enqueue(Publisher *publisher, Book *value) {
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
        sem_wait(&publisher->empty);
        printf("DEBUG: Semaphore value decreased by 1\n");
    }
    printf("DEBUG: Locking mutex for enqueue\n");
    // Lock the mutex for the buffer before writing to it.
    pthread_mutex_lock(&publisher->lock);
    int bufferwriteindex = (publisher->rear++) % publisher->buflen;
    if (bufferwriteindex < 0) {
        fprintf(stderr, "ERROR: Integer was already overflowed when trying to queue value to buffer %d\n", publisher->id);
        exit(-1);
    }
    value->bookid = ++publisher->total_book_count;
    printf("DEBUG: Adding Book%d_%d to buffer %d position %d\n", value->typeid, value->bookid, publisher->id, bufferwriteindex);
    publisher->buffer[bufferwriteindex] = value;
    printf("DEBUG: Unlocking mutex after enqueue\n");
    // Unlock the mutex after writing to the buffer is done.
    pthread_mutex_unlock(&publisher->lock);
    printf("DEBUG: Updating semaphore\n");
    // Update the full semaphore value to reflect the number of full slots in the buffer, or the number of books rear the buffer
    sem_post(&publisher->full);
}

Book *dequeue(Publisher *publisher) {
    printf("DEBUG: Waiting for items in buffer (if needed)\n");
    int semval = 0;
    sem_getvalue(&publisher->full, &semval);
    printf("!!!!!!!!!!!!!!!!!!!DEQUE Semaphore before trywait: %d\n", semval);
    int semtrywaitsuccess = sem_trywait(&publisher->full);
    sem_getvalue(&publisher->full, &semval);
    printf("!!!!!!!!!!!!!!!!!!!!DEQUE Semaphore after trywait: %d\n", semval);

    if (semtrywaitsuccess == -1 && errno == EAGAIN) { // sem_trywait() will return -1 and errno will be EAGAIN if semaphore is 0
        int threadsarealive = 0;
        for (int i = 0; i < publisher_thread_count; ++i) {
            if (publisher->threads[i] != 0) {
                sem_wait(&publisher->full); // Wait until buffer has items again.
                threadsarealive = 1;
                break;
            }
        }
        if (threadsarealive == 0) {
            return NULL;
        }
    }
    printf("DEBUG: Locking mutex for dequeue\n");
    pthread_mutex_lock(&publisher->lock);
    int bufferreadindex = (publisher->front++) % publisher->buflen;
    if (bufferreadindex < 0) {
        printf("Integer was already overflowed when trying to queue value to buffer %d\n", publisher->id);
        exit(-1);
    }
    printf("DEBUG: Reading value from buffer %d at position %d\n", publisher->id, bufferreadindex);
    Book *result = publisher->buffer[bufferreadindex];
    printf("DEBUG: Unlocking mutex after dequeue\n");
    pthread_mutex_unlock(&publisher->lock);
    printf("DEBUG: Updating semaphore\n");
    sem_post(&publisher->empty);
    return result;
}

void *publish(void *arg) {
    PublisherArgs publisherArgs = *(PublisherArgs *) arg;
    Publisher *publisher = publisherArgs.publisher;
    int publisherid = publisherArgs.publisherid;
    for (int i = 0; i < book_per_thread; ++i) {
        Book *book;
        init_book(publisher, &book);
        enqueue(publisher, book);
    }
    printf("Publisher %d of type %d\t\tFinished publishing %d books. Exiting the system.", publisherid, publisher->id, book_per_thread);
    return NULL;
}

void *package(void *arg) {
    PackagerArgs *packager_args = (PackagerArgs *) arg;
    int packagerid = packager_args->packagerid;
    Publisher **publishers = packager_args->publishers;
    Book *books[package_size];
    int i = 0;
    int nonemptyidcount = publisher_type;
    int nonemptyids[nonemptyidcount];
    for (int j = 0; j < publisher_type; ++j) {
        nonemptyids[j] = j;
    }
    while (i < package_size) {
        int random_publisher_id = nonemptyids[rand() % nonemptyidcount];
        printf("DEBUG: Random Publisher Id: %d\n", random_publisher_id);
        Publisher *publisher = publishers[random_publisher_id];
        printf("before dequeue\n");
        Book *returnval = dequeue(publisher);
        printf("exit dequeue\n");
        if (returnval == NULL) { // Buffer was empty
            nonemptyidcount -= 1;
            if (nonemptyidcount == 0) { // All buffers are empty, all publishers are dead.
                printf("There are no publishers left in the system. Only %d of %d number of books could be packaged. The package contains:\n\t\t\t\t\t\t", i, package_size);
                for (int j = 0; j < i; ++j) { // TODO Check if all books are correctly printed
                    if (j != i - 1) {
                        printf("Book%d_%d, ", books[j]->typeid, books[j]->bookid);
                    }
                    else {
                        printf("Book%d_%d.\n", books[j]->typeid, books[j]->bookid);
                    }
                }
                printf("Exiting the system.\n");
                pthread_exit(0);
            }
            for (int j = 0; j < nonemptyidcount; ++j) {
                if (j < random_publisher_id) { // 0 1 2 3 4 5
                    nonemptyids[j] = j;
                }
                else {
                    nonemptyids[j] = j + 1;
                }
            }
        }
        else {
            printf("Packager %d\t\t\tPut Book%d_%d into the package.\n", packagerid, returnval->typeid, returnval->bookid);
            books[i] = returnval;
            i++;
        }
    }
    printf("Packager %d\t\t\tFinished preparing one package. The package contains:\n", packagerid);
    printf("\t\t\t\t\t\t"); // TODO fix formatting here.
    for (int j = 0; j < package_size; ++j) {
        if (j != package_size - 1) {
            printf("Book%d_%d, ", books[j]->typeid, books[j]->bookid);
        }
        else {
            printf("Book%d_%d.\n", books[j]->typeid, books[j]->bookid);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 10) {
        fprintf(stderr,
                "Usage: project3 {-n publisher_type publisher_thread_count packager_thread_count} {-b books_per_thread} {-s package_size buffer_size}");
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
                    "Usage: project3 {-n publisher_type publisher_thread_count packager_thread_count} {-b books_per_thread} {-s package_size buffer_size}");
            exit(-1);
        }
    }
    Publisher *publishers[publisher_type];
    init(publishers);

    for (int i = 0; i < publisher_type; ++i) {
        for (int j = 0; j < publisher_thread_count; ++j) {
            PublisherArgs *publisherArgs = malloc(sizeof(PublisherArgs));
            publisherArgs->publisher = publishers[i];
            publisherArgs->publisherid = j + 1;
            pthread_create(&publishers[i]->threads[j], NULL, publish, publisherArgs);
        }
    }


    pthread_t packagerthreads[packager_thread_count];
    for (int i = 0; i < packager_thread_count; ++i) {
        PackagerArgs *packagerArgs = malloc(sizeof(PackagerArgs));
        packagerArgs->packagerid = i;
        packagerArgs->publishers = publishers;
        pthread_create(&packagerthreads[i], NULL, package, packagerArgs);
    }

    for (int i = 0; i < publisher_type; ++i) {
        for (int j = 0; j < publisher_thread_count; ++j) {
            pthread_join(publishers[i]->threads[j], NULL);
        }
    }

    for (int i = 0; i < packager_thread_count; ++i) {
        pthread_join(packagerthreads[i], NULL);
    }

    printf("Execution complete.\n");
    fprintf(stderr, "Error test.\n");
    return 0;
}
