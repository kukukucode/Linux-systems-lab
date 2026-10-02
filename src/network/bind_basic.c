#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(0),
    };

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed\n");
        close(socket_fd);
        return 1;
    }

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(socket_fd);
        return 1;
    }

    socklen_t address_length = sizeof(address);

    if (getsockname(socket_fd, (struct sockaddr *)&address, &address_length) ==
        -1) {
        perror("getsockname");
        close(socket_fd);
        return 1;
    }

    printf("bound to 127.0.0.1:%u\n", (unsigned)ntohs(address.sin_port));

    close(socket_fd);
    return 0;
}
