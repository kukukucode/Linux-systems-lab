#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
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

    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed\n");
        close(listen_fd);
        return 1;
    }

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 8) == -1) {
        perror("listen");
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

    int connection_fd = accept(listen_fd, NULL, NULL);

    if (connection_fd == -1) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    int flags = fcntl(connection_fd, F_GETFL);

    if (flags == -1) {
        perror("fcntl F_GETFL");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    if (fcntl(connection_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    printf("connection fd=%d is nonblocking\n", connection_fd);

    char buffer[128];

    ssize_t bytes_read = read(connection_fd, buffer, sizeof(buffer));

    if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("read: no data available, returned immediately\n");
    } else if (bytes_read == -1) {
        fprintf(stderr, "read failed: %s\n", strerror(errno));
    } else {
        printf("read returned %zd bytes\n", bytes_read);
    }

    close(connection_fd);
    close(listen_fd);

    return 0;
}
