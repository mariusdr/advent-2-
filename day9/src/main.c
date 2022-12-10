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
#include <stdint.h>

char *map_file(char *fn, ssize_t *len, int *fd) {
  *fd = open(fn, O_RDONLY);
  if (*fd == -1) {
    fprintf(stderr, "failed to open %s, errno = %d\n", fn, errno);
    exit(1);
  }

  // seek end of file to calculate length
  *len = lseek(*fd, 0, SEEK_END);
  if (*len == -1) {
    fprintf(stderr, "failed to calculate file len, errno = %d\n", errno);
    exit(1);
  } 
  
  void *addr = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, *fd, 0);
  if (addr == MAP_FAILED) {
    fprintf(stderr, "failed to map file into memory");
    exit(1);
  }
  return addr;
}

uint64_t calc_checksum(void *data, size_t len) {
  uint64_t checksum = 0;
  uint64_t *ptr = (uint64_t *)data;
  while ((void *)ptr < (data + len)) {
    checksum += *ptr++;
  }
  uint8_t *cptr = (uint8_t *)ptr;
  while ((void *)cptr < (data + len)) {
    checksum += *cptr++;
  }
  return checksum;
}

int main(int argc, char **argv) {
  const char *xattr = "user.checksum";
  char *fn;
  bool reset_checksum = false;
  if (argc == 3 && strncmp(argv[1], "-r", 3) == 0) {
    reset_checksum = true;
    fn = argv[2];
  } else if (argc == 2) {
    fn = argv[1];
  } else {
    fprintf(stderr, "usage %s [-r] <FILE>\n", argv[0]);
    exit(0);
  }

  ssize_t flen = -1;
  int fd = -1;
  void *faddr = map_file(fn, &flen, &fd);
  uint64_t ccsum = calc_checksum(faddr, flen);
  printf("calculated checksum for file = %lx\n", ccsum);

  if (!reset_checksum) {
    if (setxattr(fn, xattr, (void *)&ccsum, sizeof(uint64_t), XATTR_CREATE) != 0) {
      if (errno != EEXIST) {
        fprintf(stderr, "setting user.checksum failed with errno = %d\n", errno);
      }
    }
    uint64_t cursum = 0;
    if (getxattr(fn, xattr, (void *)&cursum, sizeof(uint64_t)) == -1) {
      fprintf(stderr, "failed to read user.checksum, errno = %d\n", errno);
    }
    if (cursum != ccsum) {
      fprintf(stderr, "checksums do not match (current %lx, calculated %lx)\n", cursum, ccsum);
    } else {
      fprintf(stdout, "calculated checksum and user.checksum match\n");
    }
  } else {
    if (setxattr(fn, xattr, (void *)&ccsum, sizeof(uint64_t), XATTR_REPLACE) != 0) {
      fprintf(stderr, "could not replace user.checksum, errno = %d\n", errno);
      exit(1);
    }
  }
  return 0;
}
