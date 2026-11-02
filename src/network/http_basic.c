#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

    int connection_fd = accept(listen_fd, NULL, NULL);

    if (connection_fd == -1) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    char buffer[128];

    ssize_t bytes_read = read(connection_fd, buffer, sizeof(buffer));

    if (bytes_read == -1) {
        perror("read");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    if (bytes_read > 0) {
        const char response[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 5\r\n"
            "Connection: close\r\n"
            "\r\n"
            "hello";

        size_t sent = 0;

        while (sent < sizeof(response) - 1) {
            ssize_t n = write(connection_fd, response + sent,
                              sizeof(response) - 1 - sent);

            if (n > 0) {
                sent += (size_t)n;
                continue;
            }

            if (n == -1 && errno == EINTR) {
                continue;
            }

            perror("write");
            close(connection_fd);
            close(listen_fd);
            return 1;
        }
    }

    close(connection_fd);
    close(listen_fd);

    return 0;
}
