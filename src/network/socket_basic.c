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

    printf("socket fd=%d\n", socket_fd);

    if (close(socket_fd) == -1) {
        perror("close");
        return 1;
    }

    return 0;
}
