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

    char request[4096];
    size_t received = 0;

    while (received < sizeof(request) - 1) {
        ssize_t n = read(connection_fd, request + received,
                         sizeof(request) - 1 - received);

        if (n > 0) {
            received += (size_t)n;
            request[received] = '\0';

            if (strstr(request, "\r\n\r\n") != NULL) {
                break;
            }

            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        fprintf(stderr, "incomplete HTTP request\n");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    if (strstr(request, "\r\n\r\n") == NULL) {
        fprintf(stderr, "HTTP headers too large or incomplete\n");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    char method[8];
    char path[256];
    char version[16];

    if (sscanf(request, "%7s %255s %15s",
               method, path, version) != 3) {
        fprintf(stderr, "invalid request line\n");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    printf("method=%s\n", method);
    printf("path=%s\n", path);
    printf("version=%s\n", version);

    const char *response;

    if (strcmp(path, "/hello") == 0) {
        response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 5\r\n"
            "Connection: close\r\n"
            "\r\n"
            "hello";
    } else {
        response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "Connection: close\r\n"
            "\r\n"
            "not found";
    }

    size_t response_len = strlen(response);

    size_t sent = 0;

    while (sent < response_len) {
        ssize_t n = write(connection_fd, response + sent,
                          response_len - sent);

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

    close(connection_fd);
    close(listen_fd);

    return 0;
}
