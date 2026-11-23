#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int is_token_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }

    return c != 0 && strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static int parse_request_line(const char *request, char method[8],
                              char path[256], char version[16])
{
    const char *end = strstr(request, "\r\n");

    if (end == NULL) {
        return 0;
    }

    const char *first = memchr(request, ' ', (size_t)(end - request));

    if (first == NULL || first == request) {
        return 0;
    }

    const char *second = memchr(first + 1, ' ', (size_t)(end - first - 1));

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

static size_t find_header_end(const char *data, size_t length)
{
    for (size_t i = 0; i + 4 <= length; ++i) {
        if (memcmp(data + i, "\r\n\r\n", 4) == 0) {
            return i + 4;
        }
    }

    return 0;
}

static int validate_host_header(const char *request, size_t header_end,
                                size_t *content_length)
{
    const char *limit = request + header_end;
    const char *line_end = request;

    while (line_end + 1 < limit &&
           !(line_end[0] == '\r' && line_end[1] == '\n')) {
        ++line_end;
    }

    if (line_end + 1 >= limit) {
        return 0;
    }

    const char *line = line_end + 2;
    int host_count = 0;
    int length_seen = 0;
    *content_length = 0;

    while (line + 1 < limit) {
        line_end = line;

        while (line_end + 1 < limit &&
               !(line_end[0] == '\r' && line_end[1] == '\n')) {
            ++line_end;
        }

        if (line_end + 1 >= limit) {
            return 0;
        }

        if (line == line_end) {
            return host_count == 1 && line_end + 2 == limit;
        }

        const char *colon = memchr(line, ':', (size_t)(line_end - line));

        if (colon == NULL || colon == line) {
            return 0;
        }

        for (const char *p = line; p < colon; ++p) {
            if (!is_token_char((unsigned char)*p)) {
                return 0;
            }
        }

        for (const char *p = colon + 1; p < line_end; ++p) {
            unsigned char c = (unsigned char)*p;

            if ((c < 0x20 && c != '\t') || c == 0x7f) {
                return 0;
            }
        }

        if (colon - line == 4 && strncasecmp(line, "Host", 4) == 0) {
            if (++host_count != 1) {
                return 0;
            }

            const char *value = colon + 1;

            while (value < line_end && (*value == ' ' || *value == '\t')) {
                ++value;
            }

            if (value == line_end) {
                return 0;
            }
        }

        if (colon - line == 17 &&
            strncasecmp(line, "Transfer-Encoding", 17) == 0) {
            return 0;
        }

        if (colon - line == 14 &&
            strncasecmp(line, "Content-Length", 14) == 0) {
            if (length_seen) {
                return 0;
            }

            length_seen = 1;
            const char *p = colon + 1;

            while (p < line_end && (*p == ' ' || *p == '\t')) {
                ++p;
            }

            size_t length = 0;
            int digits = 0;

            while (p < line_end && *p >= '0' && *p <= '9') {
                unsigned digit = (unsigned)(*p - '0');

                if (length > (SIZE_MAX - digit) / 10) {
                    return 0;
                }

                length = length * 10 + digit;
                digits = 1;
                ++p;
            }

            while (p < line_end && (*p == ' ' || *p == '\t')) {
                ++p;
            }

            if (!digits || p != line_end) {
                return 0;
            }

            *content_length = length;
        }

        line = line_end + 2;
    }

    return 0;
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

    struct timespec deadline;

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1) {
        perror("clock_gettime");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    deadline.tv_sec += 2;

    char request[4096];
    size_t received = 0;
    size_t header_end = 0;
    int peer_eof = 0;
    int recv_timeout = 0;

    while (received < sizeof(request) - 1) {
        struct timespec now;

        if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
            perror("clock_gettime");
            close(connection_fd);
            close(listen_fd);
            return 1;
        }

        time_t seconds = deadline.tv_sec - now.tv_sec;
        long nanoseconds = deadline.tv_nsec - now.tv_nsec;

        if (nanoseconds < 0) {
            --seconds;
            nanoseconds += 1000000000L;
        }

        if (seconds < 0 || (seconds == 0 && nanoseconds == 0)) {
            recv_timeout = 1;
            break;
        }

        struct timeval timeout = {
            .tv_sec = seconds,
            .tv_usec = nanoseconds / 1000,
        };

        if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
            timeout.tv_usec = 1;
        }

        if (setsockopt(connection_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                       sizeof(timeout)) == -1) {
            perror("setsockopt");
            close(connection_fd);
            close(listen_fd);
            return 1;
        }

        ssize_t n = read(connection_fd, request + received,
                         sizeof(request) - 1 - received);

        if (n > 0) {
            received += (size_t)n;
            request[received] = '\0';

            header_end = find_header_end(request, received);

            if (header_end != 0) {
                break;
            }

            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            recv_timeout = 1;
            break;
        }

        if (n == 0) {
            peer_eof = 1;
            break;
        }

        fprintf(stderr, "incomplete HTTP request\n");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    struct timespec finished;

    if (clock_gettime(CLOCK_MONOTONIC, &finished) == -1) {
        perror("clock_gettime");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    if (finished.tv_sec > deadline.tv_sec ||
        (finished.tv_sec == deadline.tv_sec &&
         finished.tv_nsec >= deadline.tv_nsec)) {
        recv_timeout = 1;
    }

    char method[8];
    char path[256];
    char version[16];

    size_t content_length = 0;

    int valid = header_end != 0 && memchr(request, '\0', header_end) == NULL &&
                parse_request_line(request, method, path, version) &&
                validate_host_header(request, header_end, &content_length);

    int body_eof = 0;
    size_t body_received = 0;

    if (valid && !recv_timeout) {
        size_t buffered = received - header_end;

        body_received = buffered < content_length ? buffered : content_length;

        while (body_received < content_length) {
            struct timespec now;

            if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
                perror("clock_gettime");
                close(connection_fd);
                close(listen_fd);
                return 1;
            }

            time_t seconds = deadline.tv_sec - now.tv_sec;
            long nanoseconds = deadline.tv_nsec - now.tv_nsec;

            if (nanoseconds < 0) {
                --seconds;
                nanoseconds += 1000000000L;
            }

            if (seconds < 0 || (seconds == 0 && nanoseconds == 0)) {
                recv_timeout = 1;
                break;
            }

            struct timeval timeout = {
                .tv_sec = seconds,
                .tv_usec = nanoseconds / 1000,
            };

            if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
                timeout.tv_usec = 1;
            }

            if (setsockopt(connection_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                           sizeof(timeout)) == -1) {
                perror("setsockopt");
                close(connection_fd);
                close(listen_fd);
                return 1;
            }

            char chunk[1024];
            size_t remaining = content_length - body_received;
            size_t wanted =
                remaining < sizeof(chunk) ? remaining : sizeof(chunk);

            ssize_t n = read(connection_fd, chunk, wanted);

            if (n > 0) {
                body_received += (size_t)n;
                continue;
            }

            if (n == 0) {
                body_eof = 1;
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                recv_timeout = 1;
                break;
            }

            perror("read body");
            close(connection_fd);
            close(listen_fd);
            return 1;
        }

        printf("body_received=%zu\n", body_received);
    }

    if (valid) {
        printf("method=%s\n", method);
        printf("path=%s\n", path);
        printf("version=%s\n", version);
        printf("content_length=%zu\n", content_length);
    }

    const char *response;

    if (recv_timeout) {
        response = "HTTP/1.1 408 Request Timeout\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 15\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "request timeout";
    } else if (header_end == 0 && !peer_eof) {
        response = "HTTP/1.1 431 Request Header Fields Too Large\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 17\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "headers too large";
    } else if (!valid || body_eof) {
        response = "HTTP/1.1 400 Bad Request\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 11\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "bad request";
    } else if (strcmp(path, "/hello") == 0 && strcmp(method, "GET") != 0) {
        response = "HTTP/1.1 405 Method Not Allowed\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 18\r\n"
                   "Allow: GET\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "method not allowed";
    } else if (strcmp(path, "/hello") == 0) {
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 5\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "hello";
    } else {
        response = "HTTP/1.1 404 Not Found\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: 9\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "not found";
    }

    size_t response_len = strlen(response);

    size_t sent = 0;

    while (sent < response_len) {
        ssize_t n = write(connection_fd, response + sent, response_len - sent);

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
