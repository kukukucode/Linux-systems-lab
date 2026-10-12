#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
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

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
        bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(listen_fd, 8) == -1) {
        perror("server setup");
        close(listen_fd);
        return 1;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        return 1;
    }

    struct epoll_event event = {
        .events = EPOLLIN,
        .data.fd = listen_fd,
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl");
        close(epoll_fd);
        close(listen_fd);
        return 1;
    }

    socklen_t address_length = sizeof(address);

    if (getsockname(listen_fd, (struct sockaddr *)&address, &address_length) ==
        -1) {
        perror("getsockname");
        close(epoll_fd);
        close(listen_fd);
        return 1;
    }

    printf("listening on 127.0.0.1:%u\n", (unsigned)ntohs(address.sin_port));
    printf("listen fd=%d epoll fd=%d\n", listen_fd, epoll_fd);
    printf("waiting with epoll_wait()\n");
    fflush(stdout);

    struct epoll_event ready_event;

    int ready = epoll_wait(epoll_fd, &ready_event, 1, -1);

    if (ready == -1) {
        perror("epoll_wait");
        close(epoll_fd);
        close(listen_fd);
        return 1;
    }

    printf("epoll_wait: ready=%d fd=%d events=0x%x\n", ready,
           ready_event.data.fd, ready_event.events);

    if (ready_event.events & EPOLLIN) {
        int connection_fd = accept(listen_fd, NULL, NULL);

        if (connection_fd == -1) {
            perror("accept");
        } else {
            printf("accepted connection fd=%d\n", connection_fd);
            close(connection_fd);
        }
    }

    close(epoll_fd);
    close(listen_fd);

    return 0;
}
