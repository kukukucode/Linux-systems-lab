#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define QUEUE_CAPACITY 4
#define WORKER_COUNT 3

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static int queue[QUEUE_CAPACITY];
static int head = 0;
static int tail = 0;
static int count = 0;

static int take_connection(void)
{
    pthread_mutex_lock(&mutex);

    while (count == 0) {
        pthread_cond_wait(&not_empty, &mutex);
    }

    int connection_fd = queue[head];
    head = (head + 1) % QUEUE_CAPACITY;
    --count;

    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&mutex);

    return connection_fd;
}

static void submit_connection(int connection_fd)
{
    pthread_mutex_lock(&mutex);

    while (count == QUEUE_CAPACITY) {
        pthread_cond_wait(&not_full, &mutex);
    }

    queue[tail] = connection_fd;
    tail = (tail + 1) % QUEUE_CAPACITY;
    ++count;

    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mutex);
}

static void *worker(void *argument)
{
    int id = *(int *)argument;

    for (;;) {
        int connection_fd = take_connection();

        char buffer[128];

        ssize_t bytes_read = read(connection_fd, buffer, sizeof(buffer));

        if (bytes_read == -1) {
            perror("read");
            close(connection_fd);
            continue;
        }

        if (bytes_read > 0) {
            ssize_t bytes_written =
                write(connection_fd, buffer, (size_t)bytes_read);

            if (bytes_written == -1) {
                perror("write");
            }
        }

        printf("worker %d: handled fd=%d\n", id, connection_fd);

        close(connection_fd);
    }

    return NULL;
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

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(0),
    };

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed\n");
        close(listen_fd);
        return 1;
    }

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 8) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    socklen_t address_length = sizeof(address);

    if (getsockname(listen_fd, (struct sockaddr *)&address, &address_length) ==
        -1) {
        perror("getsockname");
        close(listen_fd);
        return 1;
    }

    printf("listening on 127.0.0.1:%u\n", (unsigned)ntohs(address.sin_port));
    fflush(stdout);

    for (;;) {
        int connection_fd = accept(listen_fd, NULL, NULL);

        if (connection_fd == -1) {
            perror("accept");
            continue;
        }

        submit_connection(connection_fd);
    }
}
