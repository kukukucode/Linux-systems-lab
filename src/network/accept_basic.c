#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
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

    struct sockaddr_in peer;
    socklen_t peer_length = sizeof(peer);

    int connection_fd =
        accept(listen_fd, (struct sockaddr *)&peer, &peer_length);

    if (connection_fd == -1) {
        perror("accept");
        close(listen_fd);
        return 1;
    }

    char peer_address[INET_ADDRSTRLEN];

    if (inet_ntop(AF_INET, &peer.sin_addr, peer_address,
                  sizeof(peer_address)) == NULL) {
        perror("inet_ntop");
        close(connection_fd);
        close(listen_fd);
        return 1;
    }

    printf("accepted fd=%d from %s:%u\n", connection_fd, peer_address,
           (unsigned)ntohs(peer.sin_port));

    close(connection_fd);
    close(listen_fd);

    return 0;
}
