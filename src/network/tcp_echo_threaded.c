#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void *handle_connection(void *argument)
{
    int connection_fd = *(int *)argument;
    free(argument);

    char buffer[128];

    ssize_t bytes_read = read(connection_fd, buffer, sizeof(buffer));

    if (bytes_read == -1) {
        perror("read");
        close(connection_fd);
        return NULL;
    }

    if (bytes_read > 0) {
        ssize_t bytes_written =
            write(connection_fd, buffer, (size_t)bytes_read);

        if (bytes_written == -1) {
            perror("write");
        }
    }

    close(connection_fd);
    return NULL;
}

int main(void)
{
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

        int *thread_fd = malloc(sizeof(*thread_fd));

        if (thread_fd == NULL) {
            perror("malloc");
            close(connection_fd);
            continue;
        }

        *thread_fd = connection_fd;

        pthread_t thread;

        int error = pthread_create(&thread, NULL, handle_connection, thread_fd);

        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            free(thread_fd);
            close(connection_fd);
            continue;
        }

        error = pthread_detach(thread);

        if (error != 0) {
            fprintf(stderr, "pthread_detach failed: %s\n", strerror(error));
        }
    }
}
