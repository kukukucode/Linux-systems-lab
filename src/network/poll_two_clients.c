#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define CLIENT_COUNT 2

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

    struct pollfd clients[CLIENT_COUNT];

    for (int i = 0; i < CLIENT_COUNT; ++i) {
        int fd = accept(listen_fd, NULL, NULL);

        if (fd == -1) {
            perror("accept");

            for (int j = 0; j < i; ++j) {
                close(clients[j].fd);
            }

            close(listen_fd);
            return 1;
        }

        clients[i].fd = fd;
        clients[i].events = POLLIN;
        clients[i].revents = 0;

        printf("accepted client %d fd=%d\n", i + 1, fd);
        fflush(stdout);
    }

    printf("poll: waiting for either client\n");
    fflush(stdout);

    int ready = poll(clients, CLIENT_COUNT, -1);

    if (ready == -1) {
        perror("poll");
    } else {
        printf("poll: ready=%d\n", ready);

        for (int i = 0; i < CLIENT_COUNT; ++i) {
            printf("client %d: revents=0x%x\n", i + 1, clients[i].revents);

            if (clients[i].revents & POLLIN) {
                char buffer[128];

                ssize_t n = read(clients[i].fd, buffer, sizeof(buffer));

                if (n > 0) {
                    printf("client %d: read %zd bytes\n", i + 1, n);
                } else if (n == 0) {
                    printf("client %d: disconnected\n", i + 1);
                } else {
                    perror("read");
                }
            }
        }
    }

    for (int i = 0; i < CLIENT_COUNT; ++i) {
        close(clients[i].fd);
    }

    close(listen_fd);
    return ready == -1 ? 1 : 0;
}
