#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define handle_error(msg)                                                      \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

struct client {
  int socketFd;
  struct sockaddr_in addr;
  bool active;
  bool done;
};

struct clientInfo {
  int socketFd;
  struct sockaddr_in addr;
  int index;
};

pthread_mutex_t clientsLock = PTHREAD_MUTEX_INITIALIZER;
struct client *clients = NULL;
int numberOfClients = 0;
int doneCount = 0;
int serverFd = -1;
atomic_bool serverRunning = ATOMIC_VAR_INIT(true);

char *findChar(char *buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    if (buf[i] == '\n')
      return &buf[i];

  return NULL;
}

int writeAll(int fd, const uint8_t *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    // buf + sent is start of msg + bytes alr sent
    // len - sent are remaining bytes
    ssize_t n = send(fd, buf + sent, len - sent, 0);

    if (n > 0) {
      sent += (size_t)n;
      continue;
    }

    if (n == -1 && errno == EINTR)
      continue;
    return -1;
  }
  return 0;
}

void *threadAccept(void *arg) {
  //Threads accept a single void* argument, so we typecast it to the clientInfo struct
  struct clientInfo *clientInfo = (struct clientInfo *)arg;
  char buf[2048];
  size_t used = 0;

  while (1) {
    ssize_t bytes =
        recv(clientInfo->socketFd, buf + used, sizeof(buf) - used, 0);
    // type is 0 if its a regular message, 1 if end of client execution

    if (bytes <= 0) {
      // mark client as inactive and close its socket locked to avoid race conditions
      pthread_mutex_lock(&clientsLock);

      clients[clientInfo->index].active = false;
      close(clientInfo->socketFd);

      pthread_mutex_unlock(&clientsLock);
      free(clientInfo);
      return NULL;
    }
    used += (size_t)bytes;
    uint8_t type = (uint8_t)buf[0];

    size_t start = 0;
    while (1) {
      // one byte for type
      if (used - start < 1)
        break;

      uint8_t type = buf[start];

      if (type == 0) {
        // look for newline to find end of msg
        char *newLine = memchr(buf + start + 1, '\n', used - start - 1);

        if (!newLine)
          break;
        
          // 1 for type + msgLen for msg + \n
        size_t msgLen = (size_t)((char *)newLine - (char *)(buf + start + 1)) + 1;
        size_t consBytes = 1 + msgLen;
        uint8_t output[1024];
        output[0] = 0;

        // IP
        memcpy(output + 1, &clientInfo->addr.sin_addr.s_addr, 4);
        // Port
        memcpy(output + 5, &clientInfo->addr.sin_port, 2);
        // Message
        memcpy(output + 7, buf + start + 1, msgLen);

        size_t totalLen = 7 + msgLen;

        // send to all active clients
        pthread_mutex_lock(&clientsLock);

        for (int i = 0; i < numberOfClients; i++)
          if (clients[i].active &&
              writeAll(clients[i].socketFd, output, totalLen) == -1)
            clients[i].active = false;

        pthread_mutex_unlock(&clientsLock);
        start += consBytes;

      } else if (type == 1) {
        if (used - start < 2)
          break;

        pthread_mutex_lock(&clientsLock);

        if (!clients[clientInfo->index].done) {
          clients[clientInfo->index].done = true;
          doneCount++;
        }

        // if all clients done, notify all and shutdown server
        if (doneCount == numberOfClients) {
          uint8_t msg[2] = {1, '\n'};

          // notify all clients
          for (int i = 0; i < numberOfClients; i++)
            if (clients[i].active)
              writeAll(clients[i].socketFd, msg, 2);
          atomic_store(&serverRunning, false);
          close(serverFd);
        }
        pthread_mutex_unlock(&clientsLock);
        start += 2;
      } else {
        start = used;
        break;
      }
    }
    // remove used bytes from buffer
    if (start) {
      memmove(buf, buf + start, used - start);
      used -= start;
    }
  }
}
int main(int argc, char *argv[]) {
  // Ignore SIGPIPE to prevent the server from terminating if a client closes the connection.
  // so we can handle it and treat as a client disconnect
  signal(SIGPIPE, SIG_IGN);
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port> <#clients>\n", argv[0]);
    return 1;
  }

  int port = atoi(argv[1]);
  numberOfClients = atoi(argv[2]);

  if (numberOfClients <= 0 || port <= 0)
    handle_error("bad args");

  // create/bind/listen
  //  serverFd is global to be used in threadAccept to close when all clients done
  //  and in main to close on error
  serverFd = socket(AF_INET, SOCK_STREAM, 0);

  if (serverFd == -1)
    handle_error("socket error");

  int yes = 1;
  // allow reuse of local addresses (port) without waiting for timeout
  setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

  struct sockaddr_in serverAddr = {0};
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(port);
  serverAddr.sin_addr.s_addr = INADDR_ANY;

  // bind to all interfaces on port
  if (bind(serverFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    handle_error("bind");

    // listen for connections with a backlog of numberOfClients
  if (listen(serverFd, numberOfClients) == -1)
    handle_error("listen");

  // array of clients to keep track of active connections
  clients = calloc(numberOfClients, sizeof(struct client));

  if (!clients)
    handle_error("calloc");

  int clientCount = 0;

  // Accept clients until we reach max clients or an error occurs
  while (atomic_load(&serverRunning)) {
    struct sockaddr_in tempClient;
    socklen_t addrLen = sizeof(tempClient);
    // accept a new client connection
    int clientFd = accept(serverFd, (struct sockaddr *)&tempClient, &addrLen);

    if (clientFd == -1) {
      if (errno == EBADF)
        break;
      if (errno == EINTR)
        continue;
      continue;
    }

    // add client to array and create thread to handle it avoid blocking main thread
    pthread_mutex_lock(&clientsLock);

    int slot = -1;

    // find first available slot
    for (size_t i = 0; i < numberOfClients; i++) {
      if (!clients[i].active) {
        slot = i;
        break;
      }
    }
    if (slot == -1) {
      pthread_mutex_unlock(&clientsLock);
      close(clientFd);
      continue;
    }
    // add client data to client at slot
    clients[slot].socketFd = clientFd;
    clients[slot].addr = tempClient;
    clients[slot].active = true;
    clients[slot].done = false;

    // allocates memory to store data for the new client
    // use clientInfo struct to pass to threadAccept client arguments
    struct clientInfo *clientInfo = malloc(sizeof(struct clientInfo));
    if (clientInfo == NULL) {
      close(clientFd);
      pthread_mutex_unlock(&clientsLock);
      continue;
    }

    clientInfo->socketFd = clientFd;
    clientInfo->addr = tempClient;
    clientInfo->index = slot;

    pthread_t clientThread;
    // create detached thread to handle client so we don't have to join it later
    pthread_create(&clientThread, NULL, threadAccept, clientInfo);
    pthread_detach(clientThread);

    pthread_mutex_unlock(&clientsLock);
  }

  free(clients);
  return 0;
}
