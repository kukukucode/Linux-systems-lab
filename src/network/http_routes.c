#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int is_token_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }

    return c != 0 && strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static int parse_request_line(const char *request,
                              char method[8],
                              char path[256],
                              char version[16])
{
    const char *end = strstr(request, "\r\n");

    if (end == NULL) {
        return 0;
    }

    const char *first = memchr(request, ' ', (size_t)(end - request));

    if (first == NULL || first == request) {
        return 0;
    }

    const char *second =
        memchr(first + 1, ' ', (size_t)(end - first - 1));

    if (second == NULL || second == first + 1) {
        return 0;
    }

    size_t method_len = (size_t)(first - request);
    size_t path_len = (size_t)(second - first - 1);
    size_t version_len = (size_t)(end - second - 1);

    if (method_len >= 8 || path_len >= 256 ||
        version_len != sizeof("HTTP/1.1") - 1) {
        return 0;
    }

    if (memcmp(second + 1, "HTTP/1.1", version_len) != 0) {
        return 0;
    }

    for (const char *p = request; p < first; ++p) {
        if (!is_token_char((unsigned char)*p)) {
            return 0;
        }
    }

    if (first[1] != '/') {
        return 0;
    }

    for (const char *p = first + 1; p < second; ++p) {
        unsigned char c = (unsigned char)*p;

        if (c <= 0x20 || c >= 0x7f) {
            return 0;
        }
    }

    memcpy(method, request, method_len);
    method[method_len] = '\0';

    memcpy(path, first + 1, path_len);
    path[path_len] = '\0';

    memcpy(version, second + 1, version_len);
    version[version_len] = '\0';

    return 1;
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

    int valid = parse_request_line(request, method, path, version);

    if (valid) {
        printf("method=%s\n", method);
        printf("path=%s\n", path);
        printf("version=%s\n", version);
    }

    const char *response;

    if (!valid) {
        response =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 11\r\n"
            "Connection: close\r\n"
            "\r\n"
            "bad request";
    } else if (strcmp(path, "/hello") == 0 &&
        strcmp(method, "GET") != 0) {
        response =
            "HTTP/1.1 405 Method Not Allowed\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 18\r\n"
            "Allow: GET\r\n"
            "Connection: close\r\n"
            "\r\n"
            "method not allowed";
    } else if (strcmp(path, "/hello") == 0) {
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
