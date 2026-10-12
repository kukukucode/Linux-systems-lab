#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

static void run_mode(const char *name, uint32_t mode)
{
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        perror("socketpair");
        exit(EXIT_FAILURE);
    }

    int flags = fcntl(sockets[0], F_GETFL);

    if (flags == -1 || fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event event = {
        .events = EPOLLIN | mode,
        .data.fd = sockets[0],
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockets[0], &event) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    const char message[] = "ABCDEFGH";

    if (write(sockets[1], message, sizeof(message) - 1) != 8) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ready_event;

    int first = epoll_wait(epoll_fd, &ready_event, 1, 1000);

    if (first != 1) {
        fprintf(stderr, "%s: first epoll_wait failed\n", name);
        exit(EXIT_FAILURE);
    }

    char buffer[3];

    ssize_t n = read(sockets[0], buffer, sizeof(buffer));

    if (n != 3) {
        fprintf(stderr, "%s: expected to read 3 bytes\n", name);
        exit(EXIT_FAILURE);
    }

    int second = epoll_wait(epoll_fd, &ready_event, 1, 0);

    if (second == -1) {
        perror("epoll_wait");
        exit(EXIT_FAILURE);
    }

    char remaining[8];
    ssize_t rest = read(sockets[0], remaining, sizeof(remaining));

    printf("%s: first ready=%d\n", name, first);
    printf("%s: first read=%zd bytes\n", name, n);
    printf("%s: second ready=%d\n", name, second);
    printf("%s: remaining readable=%zd bytes\n", name, rest);

    close(epoll_fd);
    close(sockets[0]);
    close(sockets[1]);
}

int main(void)
{
    run_mode("LT", 0);
    run_mode("ET", EPOLLET);

    return 0;
}
