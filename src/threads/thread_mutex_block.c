#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *argument)
{
    (void)argument;

    printf("worker: waiting for mutex\n");

    int error = pthread_mutex_lock(&mutex);

    if (error != 0) {
        fprintf(stderr, "pthread_mutex_lock failed: %s\n",
                strerror(error));
        return NULL;
    }

    printf("worker: acquired mutex\n");

    pthread_mutex_unlock(&mutex);

    return NULL;
}

int main(void)
{
    pthread_t thread;

    int error = pthread_mutex_lock(&mutex);

    if (error != 0) {
        fprintf(stderr, "pthread_mutex_lock failed: %s\n",
                strerror(error));
        return 1;
    }

    printf("main: mutex locked\n");

    error = pthread_create(&thread, NULL, worker, NULL);

    if (error != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
        return 1;
    }

    sleep(1);

    printf("main: unlocking mutex\n");

    pthread_mutex_unlock(&mutex);

    pthread_join(thread, NULL);
    pthread_mutex_destroy(&mutex);

    return 0;
}
