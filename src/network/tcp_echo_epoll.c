#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 4
#define MAX_EVENTS 8
#define OUTPUT_CAPACITY 65536
#define IDLE_TIMEOUT_MS 5000
#define CHECK_INTERVAL_MS 1000

struct client_state {
    int fd;
    char output[OUTPUT_CAPACITY];
    size_t output_len;
    size_t output_offset;
    int peer_eof;
    int64_t last_activity_ms;
};

static volatile sig_atomic_t stop_requested = 0;

static void handle_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int64_t monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return (int64_t)now.tv_sec * 1000 +
           now.tv_nsec / 1000000;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags == -1) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void remove_client(int epoll_fd, int fd, int *active_clients,
                          struct client_state clients[MAX_CLIENTS])
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd == fd) {
            clients[i].fd = -1;
            clients[i].output_len = 0;
            clients[i].output_offset = 0;
            clients[i].peer_eof = 0;
            break;
        }
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl DEL");
    }

    printf("closed fd=%d\n", fd);
    close(fd);
    --(*active_clients);
}

int main(void)
{
    struct sigaction action = {0};
    action.sa_handler = handle_stop;

    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

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
    struct client_state clients[MAX_CLIENTS] = {0};

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i].fd = -1;
    }

    struct epoll_event events[MAX_EVENTS];

    while (!stop_requested) {
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, CHECK_INTERVAL_MS);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < ready; ++i) {
            if (stop_requested) {
                break;
            }

            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                if (!(events[i].events & EPOLLIN)) {
                    continue;
                }

                for (;;) {
                    if (stop_requested) {
                        break;
                    }

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

                    int slot = -1;

                    for (int i = 0; i < MAX_CLIENTS; ++i) {
                        if (clients[i].fd == -1) {
                            slot = i;
                            break;
                        }
                    }

                    if (slot == -1) {
                        fprintf(stderr, "no free client slot\n");
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

                    clients[slot].fd = client_fd;
                    clients[slot].output_len = 0;
                    clients[slot].output_offset = 0;
                    clients[slot].peer_eof = 0;
                    clients[slot].last_activity_ms = monotonic_ms();

                    printf("client slot=%d fd=%d\n", slot, client_fd);
                    ++active_clients;
                    printf("accepted fd=%d active=%d\n", client_fd,
                           active_clients);
                }

                continue;
            }

            int slot = -1;

            for (int j = 0; j < MAX_CLIENTS; ++j) {
                if (clients[j].fd == fd) {
                    slot = j;
                    break;
                }
            }

            if (slot == -1) {
                continue;
            }

            struct client_state *client = &clients[slot];

            if (events[i].events & EPOLLERR) {
                remove_client(epoll_fd, fd, &active_clients, clients);
                continue;
            }

            if (!client->peer_eof &&
                (events[i].events & (EPOLLIN | EPOLLHUP))) {
                size_t space = OUTPUT_CAPACITY - client->output_len;

                if (space > 0) {
                    ssize_t n = read(fd,
                                     client->output + client->output_len,
                                     space);

                    if (n > 0) {
                        client->output_len += (size_t)n;
                        client->last_activity_ms = monotonic_ms();
                        printf("fd=%d queued=%zd bytes\n", fd, n);
                    } else if (n == 0) {
                        client->peer_eof = 1;
                    } else if (errno != EAGAIN &&
                               errno != EWOULDBLOCK &&
                               errno != EINTR) {
                        perror("read");
                        remove_client(epoll_fd, fd,
                                      &active_clients, clients);
                        continue;
                    }
                }
            }

            if ((events[i].events & EPOLLOUT) &&
                client->output_offset < client->output_len) {
                size_t pending =
                    client->output_len - client->output_offset;

                ssize_t n = send(fd,
                                 client->output + client->output_offset,
                                 pending, MSG_NOSIGNAL);

                if (n > 0) {
                    client->output_offset += (size_t)n;
                    client->last_activity_ms = monotonic_ms();
                    printf("fd=%d sent=%zd pending=%zu\n",
                           fd, n,
                           client->output_len -
                               client->output_offset);
                } else if (n == -1 &&
                           (errno == EAGAIN ||
                            errno == EWOULDBLOCK ||
                            errno == EINTR)) {
                    /* Wait for another EPOLLOUT. */
                } else {
                    fprintf(stderr, "fd=%d send failed\n", fd);
                    remove_client(epoll_fd, fd,
                                  &active_clients, clients);
                    continue;
                }
            }

            if (client->output_offset == client->output_len) {
                client->output_offset = 0;
                client->output_len = 0;
            }

            if (client->peer_eof && client->output_len == 0) {
                remove_client(epoll_fd, fd, &active_clients, clients);
                continue;
            }

            struct epoll_event updated = {
                .events =
                    (client->peer_eof ||
                     client->output_len == OUTPUT_CAPACITY
                         ? 0
                         : EPOLLIN) |
                    (client->output_len > client->output_offset
                         ? EPOLLOUT
                         : 0),
                .data.fd = fd,
            };

            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD,
                          fd, &updated) == -1) {
                perror("epoll_ctl MOD");
                remove_client(epoll_fd, fd,
                              &active_clients, clients);
                continue;
            }
        }

        int64_t now = monotonic_ms();

        for (int j = 0; j < MAX_CLIENTS; ++j) {
            if (clients[j].fd == -1) {
                continue;
            }

            if (now - clients[j].last_activity_ms >= IDLE_TIMEOUT_MS) {
                int idle_fd = clients[j].fd;

                printf("idle timeout fd=%d\n", idle_fd);

                remove_client(epoll_fd, idle_fd,
                              &active_clients, clients);
            }
        }
    }

    if (stop_requested) {
        printf("shutdown requested\n");
    }

    close(listen_fd);

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd != -1) {
            remove_client(epoll_fd, clients[i].fd,
                          &active_clients, clients);
        }
    }

    close(epoll_fd);

    printf("shutdown complete\n");

    return stop_requested ? 0 : 1;
}
