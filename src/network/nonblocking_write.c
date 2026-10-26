#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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

    if (setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &send_buffer_size,
                   sizeof(send_buffer_size)) == -1) {
        perror("setsockopt");
        return 1;
    }

    int flags = fcntl(sockets[0], F_GETFL);

    if (flags == -1 || fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return 1;
    }

    char buffer[1024] = {0};
    size_t total = 0;
    int writes = 0;

    for (;;) {
        ssize_t n = write(sockets[0], buffer, sizeof(buffer));

        if (n > 0) {
            total += (size_t)n;
            ++writes;
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            printf("write: EAGAIN (send buffer full)\n");
            break;
        }

        fprintf(stderr, "unexpected write result: %zd\n", n);
        return 1;
    }

    printf("successful writes=%d\n", writes);
    printf("total written=%zu bytes\n", total);

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

    int ready = epoll_wait(epoll_fd, &event, 1, 0);

    if (ready == -1) {
        perror("epoll_wait");
        return 1;
    }

    printf("EPOLLOUT while full=%d\n", ready);

    char received[1024];
    size_t drained = 0;

    while (drained < total) {
        ssize_t n = read(sockets[1], received, sizeof(received));

        if (n > 0) {
            drained += (size_t)n;
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        fprintf(stderr, "unexpected read result: %zd\n", n);
        return 1;
    }

    printf("peer read=%zu bytes\n", drained);

    int after = epoll_wait(epoll_fd, &event, 1, 1000);

    if (after == -1) {
        perror("epoll_wait");
        return 1;
    }

    printf("EPOLLOUT after peer read=%d\n", after);

    if (after != 1 || !(event.events & EPOLLOUT)) {
        fprintf(stderr, "expected EPOLLOUT after peer read\n");
        return 1;
    }

    close(epoll_fd);
    close(sockets[0]);
    close(sockets[1]);

    return 0;
}
