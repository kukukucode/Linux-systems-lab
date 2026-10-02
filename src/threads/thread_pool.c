#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define QUEUE_CAPACITY 4
#define WORKER_COUNT 3
#define TASK_COUNT 10

struct task {
    int value;
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static struct task queue[QUEUE_CAPACITY];
static int head = 0;
static int tail = 0;
static int count = 0;
static int shutting_down = 0;

static void *worker(void *argument)
{
    int id = *(int *)argument;

    for (;;) {
        pthread_mutex_lock(&mutex);

        while (count == 0 && !shutting_down) {
            pthread_cond_wait(&not_empty, &mutex);
        }

        if (count == 0 && shutting_down) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        struct task task = queue[head];
        head = (head + 1) % QUEUE_CAPACITY;
        --count;

        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&mutex);

        int result = task.value * task.value;

        printf("worker %d: task=%d result=%d\n", id, task.value, result);

        const struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 100000000L,
        };

        nanosleep(&delay, NULL);
    }

    printf("worker %d: exiting\n", id);

    return NULL;
}

static void submit_task(struct task task)
{
    pthread_mutex_lock(&mutex);

    while (count == QUEUE_CAPACITY) {
        pthread_cond_wait(&not_full, &mutex);
    }

    queue[tail] = task;
    tail = (tail + 1) % QUEUE_CAPACITY;
    ++count;

    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mutex);
}

int main(void)
{
    pthread_t workers[WORKER_COUNT];
    int worker_ids[WORKER_COUNT];

    for (int i = 0; i < WORKER_COUNT; ++i) {
        worker_ids[i] = i + 1;

        int error = pthread_create(&workers[i], NULL, worker, &worker_ids[i]);

        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            return 1;
        }
    }

    for (int value = 1; value <= TASK_COUNT; ++value) {
        struct task task = {
            .value = value,
        };

        submit_task(task);
    }

    pthread_mutex_lock(&mutex);

    shutting_down = 1;
    pthread_cond_broadcast(&not_empty);

    pthread_mutex_unlock(&mutex);

    for (int i = 0; i < WORKER_COUNT; ++i) {
        pthread_join(workers[i], NULL);
    }

    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&not_full);
    pthread_mutex_destroy(&mutex);

    return 0;
}
