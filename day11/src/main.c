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
#include <stdint.h>

#define RWBUFLEN 4096

ssize_t copy_write(int infd, int outfd, int *syscalls) {
  ssize_t rc = 0;
  ssize_t total = 0;
  *syscalls = 0;

  char buf[RWBUFLEN];
  while ((rc = read(infd, buf, RWBUFLEN)) > 0) {
    if (rc == -1) {
      fprintf(stderr, "read failed with errno %d", errno);
      return rc;
    }
    *syscalls += 1;
    if ((rc = write(outfd, buf, rc)) == -1) {
      fprintf(stderr, "write failed with errno %d", errno);
      return rc;
    }
    *syscalls += 2;
    total += rc;
  }
  return total;
}

ssize_t copy_sendfile(int infd, int outfd, int *syscalls) {
  *syscalls = 0;

  size_t len = lseek(infd, 0, SEEK_END);
  if (len < 0) {
    fprintf(stderr, "lseek failed to find the end\n");
    return -1;
  }

  if (lseek(infd, 0, SEEK_SET) < 0) {
    fprintf(stderr, "lseek failed to reset file to beginning\n");
    return -1;
  }

  ssize_t rc = sendfile(outfd, infd, 0, len);
  if (rc == -1) {
    fprintf(stderr, "sendfile failed with errno = %d\n", errno);
    return -1;
  }

  *syscalls += 1;
  if (rc < len) {
    fprintf(stderr, "sendfile wrote less than len bytes\n");
    return -1;
  }
  return rc;
}

double measure(int fdin, int fdout, char *banner, ssize_t (*copyfn)(int, int, int *)) {
  if (lseek(fdin, 0, SEEK_SET) < 0) {
    fprintf(stderr, "lseek failed on fdin");
    exit(1);
  }

  if (ftruncate(fdout, 0) < 0) {
    fprintf(stderr, "ftruncate failed on fdout");
    exit(1);
  }

  struct timespec start;
  if (clock_gettime(CLOCK_REALTIME, &start) < 0) {
    fprintf(stderr, "clock_gettime failed at start");
    exit(1);
  }

  int syscalls;
  ssize_t bytes = copyfn(fdin, fdout, &syscalls);

  struct timespec end;
  if (clock_gettime(CLOCK_REALTIME, &end) < 0) {
    fprintf(stderr, "clock_gettime failed at end");
    exit(1);
  }

  double delta = end.tv_sec - start.tv_sec;
  delta += (end.tv_nsec - start.tv_nsec) / 1e9;
  printf("[%10s] copied with %.2f MiB/s (in %.2fs, %d syscalls\n", banner,
         (bytes / delta) / 1024.0 / 1024.0, delta, syscalls);
  return bytes / delta;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s FILE\n", argv[0]);
    return -1;
  } 
  
  int fdin = open(argv[1], O_RDONLY);
  if (fdin < 0) {
    fprintf(stderr, "open failed on fdin\n");
    exit(1);
  }

  // output file is a in-memory file to avoid having the file system
  // influence the measured time
  int fdout = memfd_create("target", 0);

  char *ROUNDS = getenv("ROUNDS");
  int rounds = atoi(ROUNDS ? ROUNDS : "10");

  // run algo once to "warm up" the buffer cache,
  // afterwards fdin should reside in the buffer cache
  int dummy;
  copy_write(fdin, fdout, &dummy);

  // actual measurement
  double sendfile = 0;
  double write = 0;
  for (int i = 0; i < rounds; ++i) {
    sendfile += measure(fdin, fdout, "sendfile", copy_sendfile);
    write += measure(fdin, fdout, "read/write", copy_write);
  }

  printf("sendfile: %.2f MiB/s\n", sendfile / rounds / (1024 * 1024));
  printf("read/write: %.2f MiB/s\n", write / rounds / (1024 * 1024));

  return 0;
}