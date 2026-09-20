#include <pthread.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    int error = pthread_mutex_lock(&mutex);

    if (error != 0) {
        fprintf(stderr, "pthread_mutex_lock failed: %s\n",
                strerror(error));
        return 1;
    }

    printf("critical section\n");

    error = pthread_mutex_unlock(&mutex);

    if (error != 0) {
        fprintf(stderr, "pthread_mutex_unlock failed: %s\n",
                strerror(error));
        return 1;
    }

    pthread_mutex_destroy(&mutex);

    return 0;
}
