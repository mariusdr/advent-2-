#define _GNU_SOURCE
#include <stdio.h>
#include <spawn.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>

void epoll_add(int epollfd, int fd, int events) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.fd = fd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    fprintf(stderr, "adding to epoll ctl failed, errno = %d", errno);
    exit(1);
  }
}

void epoll_del(int epollfd, int fd) {
  if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
    fprintf(stderr, "deleting from epoll ctl failed, errno = %d", errno);
    exit(1);
  }
}

int domain_prepare(int epollfd) {
  printf("... by socket: echo 2 | nc -U socket\n");

  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fprintf(stderr, "could'nt open unix domain socket\n");
    exit(1);
  }

  char *sockname = "socket";
  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, sockname);

  unlink("socket");
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    fprintf(stderr, "failed to bind socket\n");
    exit(1);
  }

  if (listen(sockfd, 10) == -1) {
    fprintf(stderr, "failed to listen on socket\n");
    exit(1);
  }
  epoll_add(epollfd, sockfd, EPOLLIN);
  return sockfd;
}

void domain_accept(int epollfd, int sockfd, int events) {
  int clientfd = accept(sockfd, NULL, NULL);
  epoll_add(epollfd, clientfd, EPOLLIN);
}

void domain_recv(int epollfd, int sockfd, int events) {
  char buf[128];
  if (events & EPOLLIN) {
    int n = recv(sockfd, buf, sizeof(buf), 0);
    if (n < 0) {
      fprintf(stderr, "failed to recv from socket\n");
      exit(1);
    }
    while (n > 1 && buf[n-1] == '\n') {
      n--;
    }
    buf[n] = 0;

    struct ucred ucred;
    socklen_t slen = sizeof(struct ucred);
    if (getsockopt(sockfd, SOL_SOCKET, SO_PEERCRED, &ucred, &slen) < 0) {
      fprintf(stderr, "failed to get socket option\n");
      exit(1);
    }

    printf("socket (pid=%d, uid=%d, gid=%d): %s\n", ucred.pid, ucred.uid, ucred.gid, buf);
    epoll_del(epollfd, sockfd);
    close(sockfd);
  } else if (events & EPOLLHUP) {
    epoll_del(epollfd, sockfd);
    close(sockfd);
  }
}

int fifo_prepare(int epollfd) {
  printf("... by fifo: echo 1 > fifo\n");

  unlink("fifo");
  if (mknod("fifo", 0666 | S_IFIFO, 0) == -1) {
    fprintf(stderr, "failed to create fifo file\n");
    exit(1);
  }

  int fd = open("fifo", O_RDONLY | O_NONBLOCK);
  if (fd == -1) {
    fprintf(stderr, "failed to open fifo file, errno = %d\n", errno);
    exit(1);
  }
  epoll_add(epollfd, fd, EPOLLIN);
  return fd;
}

void fifo_handle(int epollfd, int fifofd, int events) {
  char buf[128];
  if (events & EPOLLIN) {
    int n = read(fifofd, buf, sizeof(buf));
    if (n < 0) {
      fprintf(stderr, "failed to read from fifofd, errno = %d\n", errno);
      exit(1);
    }
    if (n == 0) {
      goto close;
    }
    while (n > 1 && buf[n - 1] == '\n') {
      n--;
    }
    buf[n] = 0;
    printf("fifo: %s\n", buf);
  } else if (events & EPOLLHUP) {
close:
    epoll_del(epollfd, fifofd);
    close(fifofd);
    fifofd = open("fifo", O_RDONLY);
    if (fifofd == -1) {
      fprintf(stderr, "failed to reopen fifo file, errno = %d\n", errno);
      exit(1);
    }
    epoll_add(epollfd, fifofd, EPOLLIN);
  }
}

struct postbox {
  int fd;
  int (*prepare)(int epollfd);
  void (*handle)(int epollfd, int fd, int events);
};

struct postbox boxes[] = {
  {-1, fifo_prepare, fifo_handle},
  {-1, domain_prepare, domain_accept},
  {-1, NULL, domain_recv},
};

int main() {
  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    fprintf(stderr, "failed to create epoll file, errno = %d\n", errno);
    exit(1);
  }

  printf("postbox open, send request\n");
  for (int i = 0; i < 3; ++i) {
    printf("prepare %d\n", i);
    if (boxes[i].prepare) {
      boxes[i].fd = boxes[i].prepare(epollfd);
    }
  } 

  printf("start io loop\n");
  while (true) {
    struct epoll_event event[10];
    int nfds = epoll_wait(epollfd, event, 10, -1);
    printf("... woke up from epoll_wait\n");

    for (int n = 0; n < nfds; ++n) {
      int fd = event[n].data.fd;
      for (int i = 0; i < 3; ++i) {
        if (boxes[i].fd == fd || boxes[i].fd == -1) {
          boxes[i].handle(epollfd, fd, event[n].events);
          break;
        }
      }
    }
  }

  return 0;
}
