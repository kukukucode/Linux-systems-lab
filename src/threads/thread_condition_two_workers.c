#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

static int items = 0;

static void *worker(void *argument)
{
    int id = *(int *)argument;

    int error = pthread_mutex_lock(&mutex);

    if (error != 0) {
        fprintf(stderr, "worker %d: mutex lock failed: %s\n", id,
                strerror(error));
        return NULL;
    }

    while (items == 0) {
        printf("worker %d: waiting\n", id);

        error = pthread_cond_wait(&condition, &mutex);

        if (error != 0) {
            fprintf(stderr, "worker %d: cond wait failed: %s\n", id,
                    strerror(error));
            pthread_mutex_unlock(&mutex);
            return NULL;
        }
    }

    --items;

    printf("worker %d: consumed item, items=%d\n", id, items);

    pthread_mutex_unlock(&mutex);

    return NULL;
}

int main(void)
{
    pthread_t threads[2];
    int ids[2] = {1, 2};

    for (int i = 0; i < 2; ++i) {
        int error = pthread_create(&threads[i], NULL, worker, &ids[i]);

        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            return 1;
        }
    }

    sleep(1);

    pthread_mutex_lock(&mutex);

    items = 1;

    printf("main: one item, broadcast\n");

    pthread_cond_broadcast(&condition);

    pthread_mutex_unlock(&mutex);

    sleep(1);

    pthread_mutex_lock(&mutex);

    items = 1;

    printf("main: second item, signal\n");

    pthread_cond_signal(&condition);

    pthread_mutex_unlock(&mutex);

    for (int i = 0; i < 2; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_cond_destroy(&condition);
    pthread_mutex_destroy(&mutex);

    return 0;
}
