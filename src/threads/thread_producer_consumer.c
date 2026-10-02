#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define ITEM_COUNT 3

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static int slot;
static int full = 0;

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

        while (full) {
            printf("producer: waiting, slot is full\n");

            error = pthread_cond_wait(&not_full, &mutex);

            if (error != 0) {
                fprintf(stderr, "producer: cond wait failed: %s\n",
                        strerror(error));
                pthread_mutex_unlock(&mutex);
                return NULL;
            }
        }

        slot = value;
        full = 1;

        printf("producer: produced %d\n", value);

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

        while (!full) {
            printf("consumer: waiting, slot is empty\n");

            error = pthread_cond_wait(&not_empty, &mutex);

            if (error != 0) {
                fprintf(stderr, "consumer: cond wait failed: %s\n",
                        strerror(error));
                pthread_mutex_unlock(&mutex);
                return NULL;
            }
        }

        int value = slot;
        full = 0;

        printf("consumer: consumed %d\n", value);

        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

int main(void)
{
    pthread_t producer_thread;
    pthread_t consumer_thread;

    int error = pthread_create(&producer_thread, NULL, producer, NULL);

    if (error != 0) {
        fprintf(stderr, "producer pthread_create failed: %s\n",
                strerror(error));
        return 1;
    }

    error = pthread_create(&consumer_thread, NULL, consumer, NULL);

    if (error != 0) {
        fprintf(stderr, "consumer pthread_create failed: %s\n",
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
