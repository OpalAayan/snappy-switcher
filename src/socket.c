/* src/socket.c - Unix Domain Socket IPC Implementation */
#define _POSIX_C_SOURCE 200809L

#include "socket.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define LOG(fmt, ...) fprintf(stderr, "[Socket] " fmt "\n", ##__VA_ARGS__)
#define MAX_CMD_LEN 64

static int server_fd = -1;

/* Set socket to non-blocking */
static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Initialize server socket */
int init_server(void) {
  /* Remove old socket file - handles zombie instances from crashes */
  if (unlink(SOCKET_PATH) == 0) {
    LOG("Removed stale socket file from previous instance");
  } else if (errno != ENOENT) {
    LOG("Warning: Could not remove old socket: %s", strerror(errno));
  }

  server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    LOG("Failed to create socket: %s", strerror(errno));
    return -1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG("Failed to bind socket: %s", strerror(errno));
    close(server_fd);
    server_fd = -1;
    return -1;
  }

  /* Secure socket: owner read/write only (prevents local user attacks) */
  if (chmod(SOCKET_PATH, 0600) < 0) {
    LOG("Warning: Could not secure socket permissions: %s", strerror(errno));
  }

  if (listen(server_fd, 5) < 0) {
    LOG("Failed to listen: %s", strerror(errno));
    close(server_fd);
    server_fd = -1;
    return -1;
  }

  if (set_nonblocking(server_fd) < 0) {
    LOG("Failed to set non-blocking: %s", strerror(errno));
  }

  LOG("Server listening on %s", SOCKET_PATH);
  return server_fd;
}

/* Get server file descriptor */
int get_server_fd(void) { return server_fd; }

/* Accept a client connection (non-blocking) */
int accept_client(int srv_fd) {
  struct sockaddr_un client_addr;
  socklen_t client_len = sizeof(client_addr);

  int client_fd = accept(srv_fd, (struct sockaddr *)&client_addr, &client_len);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return -1; /* No pending connection */
    }
    LOG("Accept failed: %s", strerror(errno));
    return -1;
  }

  return client_fd;
}

/* Cleanup server */
void cleanup_server(int srv_fd) {
  if (srv_fd >= 0) {
    close(srv_fd);
  }
  unlink(SOCKET_PATH);
  server_fd = -1;
  LOG("Server cleaned up");
}

/* Client: Send command to daemon */
int send_command(const char *cmd) {
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    LOG("Failed to create client socket: %s", strerror(errno));
    return -1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG("Failed to connect to daemon: %s", strerror(errno));
    close(sock);
    return -1;
  }

  ssize_t written = write(sock, cmd, strlen(cmd));
  close(sock);

  if (written < 0) {
    LOG("Failed to send command: %s", strerror(errno));
    return -1;
  }

  return 0;
}

/* Check if daemon is running */
bool is_daemon_running(void) {
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    return false;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  close(sock);

  return result == 0;
}
