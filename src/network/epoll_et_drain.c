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

    int flags = fcntl(sockets[0], F_GETFL);

    if (flags == -1 || fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return 1;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    struct epoll_event event = {
        .events = EPOLLIN | EPOLLET,
        .data.fd = sockets[0],
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockets[0], &event) == -1) {
        perror("epoll_ctl");
        return 1;
    }

    const char message[] = "ABCDEFGH";

    if (write(sockets[1], message, sizeof(message) - 1) != 8) {
        fprintf(stderr, "write failed or incomplete\n");
        return 1;
    }

    struct epoll_event ready_event;

    int ready = epoll_wait(epoll_fd, &ready_event, 1, -1);

    if (ready != 1 || !(ready_event.events & EPOLLIN)) {
        fprintf(stderr, "expected EPOLLIN\n");
        return 1;
    }

    printf("first epoll_wait=%d\n", ready);

    size_t total = 0;

    for (;;) {
        char buffer[3];

        ssize_t n = read(sockets[0], buffer, sizeof(buffer));

        if (n > 0) {
            printf("read %zd bytes: %.*s\n", n, (int)n, buffer);
            total += (size_t)n;
            continue;
        }

        if (n == 0) {
            fprintf(stderr, "unexpected EOF\n");
            return 1;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("read: EAGAIN (drained)\n");
            break;
        }

        perror("read");
        return 1;
    }

    printf("total read=%zu bytes\n", total);

    int second = epoll_wait(epoll_fd, &ready_event, 1, 0);

    if (second == -1) {
        perror("epoll_wait");
        return 1;
    }

    printf("second epoll_wait=%d\n", second);

    const char next_message[] = "XYZ";

    if (write(sockets[1], next_message, 3) != 3) {
        fprintf(stderr, "second write failed\n");
        return 1;
    }

    int third = epoll_wait(epoll_fd, &ready_event, 1, 1000);

    if (third != 1 || !(ready_event.events & EPOLLIN)) {
        fprintf(stderr, "expected another EPOLLIN\n");
        return 1;
    }

    char next_buffer[3];
    ssize_t next_read = read(sockets[0], next_buffer, sizeof(next_buffer));

    printf("third epoll_wait=%d\n", third);
    printf("new data: %.*s (%zd bytes)\n", (int)next_read, next_buffer,
           next_read);

    close(epoll_fd);
    close(sockets[0]);
    close(sockets[1]);

    return total == 8 && second == 0 && next_read == 3 ? 0 : 1;
}
