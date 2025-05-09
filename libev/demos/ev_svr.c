#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT 12345
#define BUFFER_SIZE 1024

typedef struct {
    ev_io io;
    int fd;
} client_t;

// 设置 socket 为非阻塞
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 客户端读事件回调
void read_cb(EV_P_ ev_io *w, int revents) {
    client_t *client = (client_t *)w;
    char buffer[BUFFER_SIZE];
    ssize_t nread = read(client->fd, buffer, sizeof(buffer) - 1);
    if (nread > 0) {
        buffer[nread] = '\0';
        printf("Received: %s", buffer);
        write(client->fd, buffer, nread);  // echo 回客户端
    } else {
        if (nread < 0) perror("read error");
        else puts("Client disconnected");
        ev_io_stop(EV_A_ &client->io);
        close(client->fd);
        free(client);
    }
}

// 新连接事件回调
void accept_cb(EV_P_ ev_io *w, int revents) {
    int server_fd = w->fd;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        return;
    }

    set_nonblocking(client_fd);

    client_t *client = (client_t *)malloc(sizeof(client_t));
    client->fd = client_fd;
    ev_io_init(&client->io, read_cb, client_fd, EV_READ);
    ev_io_start(EV_A_ &client->io);

    printf("New client connected: %d\n", client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen");
        return 1;
    }

    set_nonblocking(server_fd);

    struct ev_loop *loop = EV_DEFAULT;

    ev_io server_watcher;
    ev_io_init(&server_watcher, accept_cb, server_fd, EV_READ);
    ev_io_start(loop, &server_watcher);

    printf("TCP server listening on port %d\n", PORT);
    ev_run(loop, 0);

    close(server_fd);
    return 0;
}
