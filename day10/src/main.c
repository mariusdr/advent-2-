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
#include <sys/xattr.h>
#include <sys/epoll.h>
#include <stdint.h>

struct proc {
  char *cmd;
  pid_t pid;
  int pstdin;
  int pstdout;
};

static int nprocs;
static struct proc *procs;

static int start_proc(struct proc *proc) {
  extern char **environ;
  char *argv[] = {"sh", "-c", proc->cmd, 0};
  
  int pstdin[2], pstdout[2];
  if (pipe2(pstdin, O_CLOEXEC) != 0) {
    fprintf(stderr, "creation of stdin pipe failed");
    return -1;
  }
  if (pipe2(pstdout, O_CLOEXEC) != 0) {
    fprintf(stderr, "creation of stdout pipe failed");
    return -1;
  }

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, pstdin[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&fa, pstdout[1], STDOUT_FILENO);
  int e;
  if (!(e = posix_spawn(&proc->pid, "/bin/sh", &fa, 0, argv, environ))) {
    posix_spawn_file_actions_destroy(&fa);
    proc->pstdin = pstdin[1];
    close(pstdin[0]);

    proc->pstdout = pstdout[0];
    close(pstdout[1]);

    return 0;
  } else {
    errno = e;
    return -1;
  }
}

void print_throughput(uint64_t *bytes, int elements) {
  static struct timespec last = { 0 };
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) < 0) {
    fprintf(stderr, "clock_gettime failed");
    return;
  }

  if (now.tv_sec > last.tv_sec && last.tv_sec > 0) {
    double delta = now.tv_sec - last.tv_sec;
    delta += (now.tv_sec - last.tv_sec) / 1e9;
    for (int i = 0; i < elements; ++i) {
      fprintf(stderr, "%.2fMiB/s ", bytes[i] / delta / 1024 / 1024);
    }
    fprintf(stderr, "\n");
    memset(bytes, 0, elements * sizeof(uint64_t));
    last = now;
  } else if (last.tv_sec == 0) {
    last = now;
  }
}

static void epoll_add(int epollfd, int fd, int events, uint64_t data) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.u64 = data;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    fprintf(stderr, "failed to add to epoll, errno = %d\n", errno);
    exit(1);
  }
}

static void epoll_del(int epollfd, int fd) {
  if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
    fprintf(stderr, "failed to delete from epoll\n");
    exit(1);
  }
}

static int copy_splice(int infd, int outfd) {
  static char buf[4096];
  int len = splice(infd, 0, outfd, 0, 4096, SPLICE_F_NONBLOCK);
  if (len >= 0) {
    return len;
  }

  if (errno == EAGAIN) {
    // splice would have blocked, try again later
    return 0;
  }

  if (errno != EINVAL) {
    fprintf(stderr, "splice failed with errno = %d\n", errno);
    exit(1);
  }

  len = read(infd, buf, 4096);
  if (len < 0) {
    fprintf(stderr, "read failed with errno = %d\n", errno);
    exit(1);
  }
  int rem = len;
  char *ptr = buf;
  do {
    rem -= write(outfd, ptr, rem);
    ptr += len; 
  } while (rem > 0);
  return len;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "usage: %s [CMD-1]", argv[0]);
    return -1;
  }
  nprocs = argc - 1;
  procs = (struct proc *)calloc(nprocs, sizeof(struct proc));
  if (!procs) {
    fprintf(stderr, "failed to allocate memory for procs array\n");
    exit(1);
  }

  for (int i = 0; i < nprocs; ++i) {
    procs[i].cmd = argv[i + 1];
    int rc = start_proc(&procs[i]);
    if (rc < 0) {
      fprintf(stderr, "starting filter failed with errno = %d\n", errno);
      return -1;
    }

    fprintf(stderr, "[%s] started filter as pid %d\n", procs[i].cmd, procs[i].pid);
  }

  int pairs = 1 + nprocs;
  int readfds[pairs];
  int writefds[pairs];
  
  readfds[0] = STDIN_FILENO;
  for (int i = 0; i < nprocs; ++i) {
    writefds[i] = procs[i].pstdin;
    readfds[i + 1] = procs[i].pstdout;
  }
  writefds[nprocs] = STDOUT_FILENO;

  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    fprintf(stderr, "epoll_create1 failed\n");
    return -1;
  }
  
  for (int i = 0; i < 1 + nprocs; ++i) {
    epoll_add(epollfd, readfds[i], EPOLLIN, i);
  }

  uint64_t bytes[pairs];
  memset(bytes, 0, sizeof(bytes));
  while (pairs > 0) {
    struct epoll_event event[10];
    int nfds = epoll_wait(epollfd, event, 10, -1);

    // fprintf(stderr, "epoll_wait ready with %d procs \n", nfds);

    for (int i = 0; i < nfds; ++i) {
      uint64_t pair = event[i].data.u64;
      if (event[i].events & EPOLLIN) {
        bytes[pair] += copy_splice(readfds[pair], writefds[pair]);
      }
      if (event[i].events & EPOLLHUP) {
        epoll_del(epollfd, readfds[pair]);
        close(readfds[pair]);
        close(writefds[pair]);
        pair--;
      }
    }
    print_throughput(bytes, pairs);
  }

  return 0;
}