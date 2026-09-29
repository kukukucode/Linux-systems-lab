#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

static int ready = 0;

static void *worker(void *argument)
{
    (void)argument;

    int error = pthread_mutex_lock(&mutex);

    if (error != 0) {
        fprintf(stderr, "worker: pthread_mutex_lock failed: %s\n",
                strerror(error));
        return NULL;
    }

    while (!ready) {
        printf("worker: waiting\n");

        error = pthread_cond_wait(&condition, &mutex);

        if (error != 0) {
            fprintf(stderr, "worker: pthread_cond_wait failed: %s\n",
                    strerror(error));
            pthread_mutex_unlock(&mutex);
            return NULL;
        }
    }

    printf("worker: ready\n");

    pthread_mutex_unlock(&mutex);

    return NULL;
}

int main(void)
{
    pthread_t thread;

    int error = pthread_create(&thread, NULL, worker, NULL);

    if (error != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
        return 1;
    }

    /*
     * Give the worker time to enter pthread_cond_wait().
     */
    sleep(1);

    error = pthread_mutex_lock(&mutex);

    if (error != 0) {
        fprintf(stderr, "main: pthread_mutex_lock failed: %s\n",
                strerror(error));
        return 1;
    }

    printf("main: setting ready\n");

    ready = 1;

    error = pthread_cond_signal(&condition);

    if (error != 0) {
        fprintf(stderr, "pthread_cond_signal failed: %s\n", strerror(error));
        pthread_mutex_unlock(&mutex);
        return 1;
    }

    pthread_mutex_unlock(&mutex);

    error = pthread_join(thread, NULL);

    if (error != 0) {
        fprintf(stderr, "pthread_join failed: %s\n", strerror(error));
        return 1;
    }

    pthread_cond_destroy(&condition);
    pthread_mutex_destroy(&mutex);

    return 0;
}
