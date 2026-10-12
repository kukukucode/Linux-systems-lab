#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 4

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags == -1) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void remove_client(struct pollfd *client)
{
    printf("closed fd=%d\n", client->fd);

    close(client->fd);

    client->fd = -1;
    client->events = 0;
    client->revents = 0;
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

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
        bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(listen_fd, 8) == -1) {
        perror("server setup");
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

    struct pollfd fds[MAX_CLIENTS + 1] = {0};

    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;

    for (int i = 1; i <= MAX_CLIENTS; ++i) {
        fds[i].fd = -1;
    }

    for (;;) {
        int ready = poll(fds, MAX_CLIENTS + 1, -1);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll");
            break;
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "listen socket error\n");
            break;
        }

        if (fds[0].revents & POLLIN) {
            int fd = accept(listen_fd, NULL, NULL);

            if (fd == -1) {
                perror("accept");
            } else if (set_nonblocking(fd) == -1) {
                perror("fcntl");
                close(fd);
            } else {
                int slot = -1;

                for (int i = 1; i <= MAX_CLIENTS; ++i) {
                    if (fds[i].fd == -1) {
                        slot = i;
                        break;
                    }
                }

                if (slot == -1) {
                    printf("too many clients\n");
                    close(fd);
                } else {
                    fds[slot].fd = fd;
                    fds[slot].events = POLLIN;
                    fds[slot].revents = 0;

                    printf("accepted fd=%d slot=%d\n", fd, slot);
                }
            }
        }

        for (int i = 1; i <= MAX_CLIENTS; ++i) {
            if (fds[i].fd == -1) {
                continue;
            }

            if (fds[i].revents & (POLLERR | POLLNVAL)) {
                remove_client(&fds[i]);
                continue;
            }

            if (fds[i].revents & (POLLIN | POLLHUP)) {
                char buffer[128];

                ssize_t n = read(fds[i].fd, buffer, sizeof(buffer));

                if (n > 0) {
                    printf("fd=%d received %zd bytes\n", fds[i].fd, n);
                } else if (n == 0) {
                    remove_client(&fds[i]);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                           errno != EINTR) {
                    perror("read");
                    remove_client(&fds[i]);
                }
            }
        }
    }

    for (int i = 1; i <= MAX_CLIENTS; ++i) {
        if (fds[i].fd != -1) {
            close(fds[i].fd);
        }
    }

    close(listen_fd);
    return 1;
}
