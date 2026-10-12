#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 4
#define MAX_EVENTS 8

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags == -1) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void remove_client(int epoll_fd, int fd, int *active_clients)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl DEL");
    }

    printf("closed fd=%d\n", fd);
    close(fd);
    --(*active_clients);
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

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(listen_fd, 8) == -1 || set_nonblocking(listen_fd) == -1) {
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

    struct epoll_event listen_event = {
        .events = EPOLLIN,
        .data.fd = listen_fd,
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_event) == -1) {
        perror("epoll_ctl ADD listen");
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
    fflush(stdout);

    int active_clients = 0;
    struct epoll_event events[MAX_EVENTS];

    for (;;) {
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < ready; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                if (!(events[i].events & EPOLLIN)) {
                    continue;
                }

                for (;;) {
                    int client_fd = accept(listen_fd, NULL, NULL);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }

                        if (errno == EINTR) {
                            continue;
                        }

                        perror("accept");
                        break;
                    }

                    if (active_clients == MAX_CLIENTS) {
                        printf("too many clients\n");
                        close(client_fd);
                        continue;
                    }

                    if (set_nonblocking(client_fd) == -1) {
                        perror("fcntl");
                        close(client_fd);
                        continue;
                    }

                    struct epoll_event client_event = {
                        .events = EPOLLIN,
                        .data.fd = client_fd,
                    };

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd,
                                  &client_event) == -1) {
                        perror("epoll_ctl ADD client");
                        close(client_fd);
                        continue;
                    }

                    ++active_clients;
                    printf("accepted fd=%d active=%d\n", client_fd,
                           active_clients);
                }

                continue;
            }

            if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                char buffer[128];

                ssize_t n = read(fd, buffer, sizeof(buffer));

                if (n > 0) {
                    printf("fd=%d received %zd bytes\n", fd, n);
                } else if (n == 0) {
                    remove_client(epoll_fd, fd, &active_clients);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                           errno != EINTR) {
                    perror("read");
                    remove_client(epoll_fd, fd, &active_clients);
                }
            }
        }
    }

    close(epoll_fd);
    close(listen_fd);
    return 1;
}
