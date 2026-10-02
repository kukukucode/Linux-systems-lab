#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_CAPACITY 3
#define ITEM_COUNT 9
#define WORKER_COUNT 3

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static int buffer[BUFFER_CAPACITY];
static int head = 0;
static int tail = 0;
static int count = 0;
static int producer_done = 0;

static void *producer(void *argument)
{
    (void)argument;

    for (int value = 1; value <= ITEM_COUNT; ++value) {
        pthread_mutex_lock(&mutex);

        while (count == BUFFER_CAPACITY) {
            pthread_cond_wait(&not_full, &mutex);
        }

        buffer[tail] = value;
        tail = (tail + 1) % BUFFER_CAPACITY;
        ++count;

        printf("producer: produced %d, count=%d\n", value, count);

        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&mutex);
    }

    pthread_mutex_lock(&mutex);

    producer_done = 1;

    printf("producer: done\n");

    pthread_cond_broadcast(&not_empty);
    pthread_mutex_unlock(&mutex);

    return NULL;
}

static void *consumer(void *argument)
{
    int id = *(int *)argument;

    for (;;) {
        pthread_mutex_lock(&mutex);

        while (count == 0 && !producer_done) {
            printf("worker %d: waiting\n", id);
            pthread_cond_wait(&not_empty, &mutex);
        }

        if (count == 0 && producer_done) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        int value = buffer[head];
        head = (head + 1) % BUFFER_CAPACITY;
        --count;

        printf("worker %d: consumed %d, count=%d\n", id, value, count);

        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&mutex);

        sleep(1);
    }

    printf("worker %d: exiting\n", id);

    return NULL;
}

int main(void)
{
    pthread_t producer_thread;
    pthread_t workers[WORKER_COUNT];
    int worker_ids[WORKER_COUNT];

    for (int i = 0; i < WORKER_COUNT; ++i) {
        worker_ids[i] = i + 1;

        int error = pthread_create(&workers[i], NULL, consumer, &worker_ids[i]);

        if (error != 0) {
            fprintf(stderr, "worker pthread_create failed: %s\n",
                    strerror(error));
            return 1;
        }
    }

    sleep(1);

    int error = pthread_create(&producer_thread, NULL, producer, NULL);

    if (error != 0) {
        fprintf(stderr, "producer pthread_create failed: %s\n",
                strerror(error));
        return 1;
    }

    pthread_join(producer_thread, NULL);

    for (int i = 0; i < WORKER_COUNT; ++i) {
        pthread_join(workers[i], NULL);
    }

    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&not_full);
    pthread_mutex_destroy(&mutex);

    return 0;
}
