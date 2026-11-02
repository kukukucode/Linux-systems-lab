#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        perror("socketpair");
        return 1;
    }

    int send_buffer_size = 4096;

    if (setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF,
                   &send_buffer_size, sizeof(send_buffer_size)) == -1) {
        perror("setsockopt");
        return 1;
    }

    int flags = fcntl(sockets[0], F_GETFL);

    if (flags == -1 ||
        fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return 1;
    }

    char message[65536];
    memset(message, 'A', sizeof(message));

    size_t offset = 0;

    ssize_t n = write(sockets[0], message, sizeof(message));

    if (n <= 0) {
        perror("write");
        return 1;
    }

    offset += (size_t)n;

    printf("requested=%zu bytes\n", sizeof(message));
    printf("first write=%zd bytes\n", n);
    printf("offset=%zu bytes\n", offset);
    printf("remaining=%zu bytes\n", sizeof(message) - offset);

    ssize_t next = write(sockets[0], message + offset,
                         sizeof(message) - offset);

    if (next > 0) {
        offset += (size_t)next;
        printf("second write=%zd bytes\n", next);
    } else if (next == -1 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("second write=EAGAIN\n");
    } else {
        perror("second write");
        return 1;
    }

    printf("remaining after retry=%zu bytes\n",
           sizeof(message) - offset);

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    struct epoll_event event = {
        .events = EPOLLOUT,
        .data.fd = sockets[0],
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockets[0], &event) == -1) {
        perror("epoll_ctl");
        return 1;
    }

    char received[sizeof(message)];
    size_t received_bytes = 0;

    while (received_bytes < sizeof(message)) {
        // Read everything already accepted by the sender.
        while (received_bytes < offset) {
            ssize_t n = read(sockets[1],
                             received + received_bytes,
                             offset - received_bytes);

            if (n > 0) {
                received_bytes += (size_t)n;
                continue;
            }

            if (n == -1 && errno == EINTR) {
                continue;
            }

            fprintf(stderr, "unexpected peer read: %zd\n", n);
            return 1;
        }

        if (offset == sizeof(message)) {
            break;
        }

        int ready = epoll_wait(epoll_fd, &event, 1, 1000);

        if (ready != 1 || !(event.events & EPOLLOUT)) {
            fprintf(stderr, "expected EPOLLOUT\n");
            return 1;
        }

        printf("EPOLLOUT ready\n");

        ssize_t n = write(sockets[0], message + offset,
                          sizeof(message) - offset);

        if (n > 0) {
            offset += (size_t)n;
            printf("write=%zd offset=%zu\n", n, offset);
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }

        perror("write retry");
        return 1;
    }

    printf("total sent=%zu bytes\n", offset);
    printf("total received=%zu bytes\n", received_bytes);

    if (memcmp(message, received, sizeof(message)) != 0) {
        fprintf(stderr, "data mismatch\n");
        return 1;
    }

    printf("data integrity=OK\n");

    close(epoll_fd);
    close(sockets[0]);
    close(sockets[1]);

    return 0;
}
