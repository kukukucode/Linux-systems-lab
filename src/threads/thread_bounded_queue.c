#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_CAPACITY 3
#define ITEM_COUNT 8

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static int buffer[BUFFER_CAPACITY];
static int head = 0;
static int tail = 0;
static int count = 0;

static void *producer(void *argument)
{
    (void)argument;

    for (int value = 1; value <= ITEM_COUNT; ++value) {
        int error = pthread_mutex_lock(&mutex);

        if (error != 0) {
            fprintf(stderr, "producer: mutex lock failed: %s\n",
                    strerror(error));
            return NULL;
        }

        while (count == BUFFER_CAPACITY) {
            printf("producer: waiting, queue is full\n");

            error = pthread_cond_wait(&not_full, &mutex);

            if (error != 0) {
                fprintf(stderr, "producer: cond wait failed: %s\n",
                        strerror(error));
                pthread_mutex_unlock(&mutex);
                return NULL;
            }
        }

        buffer[tail] = value;
        tail = (tail + 1) % BUFFER_CAPACITY;
        ++count;

        printf("producer: produced %d, count=%d\n", value, count);

        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

static void *consumer(void *argument)
{
    (void)argument;

    for (int i = 0; i < ITEM_COUNT; ++i) {
        int error = pthread_mutex_lock(&mutex);

        if (error != 0) {
            fprintf(stderr, "consumer: mutex lock failed: %s\n",
                    strerror(error));
            return NULL;
        }

        while (count == 0) {
            printf("consumer: waiting, queue is empty\n");

            error = pthread_cond_wait(&not_empty, &mutex);

            if (error != 0) {
                fprintf(stderr, "consumer: cond wait failed: %s\n",
                        strerror(error));
                pthread_mutex_unlock(&mutex);
                return NULL;
            }
        }

        int value = buffer[head];
        head = (head + 1) % BUFFER_CAPACITY;
        --count;

        printf("consumer: consumed %d, count=%d\n", value, count);

        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&mutex);

        sleep(1);
    }

    return NULL;
}

int main(void)
{
    pthread_t producer_thread;
    pthread_t consumer_thread;

    int error = pthread_create(&consumer_thread, NULL, consumer, NULL);

    if (error != 0) {
        fprintf(stderr, "consumer pthread_create failed: %s\n",
                strerror(error));
        return 1;
    }

    sleep(1);

    error = pthread_create(&producer_thread, NULL, producer, NULL);

    if (error != 0) {
        fprintf(stderr, "producer pthread_create failed: %s\n",
                strerror(error));
        return 1;
    }

    pthread_join(producer_thread, NULL);
    pthread_join(consumer_thread, NULL);

    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&not_full);
    pthread_mutex_destroy(&mutex);

    return 0;
}
