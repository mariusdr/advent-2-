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

struct iodata {
  struct iovec *iov;
  size_t entries; 
  size_t allocated;
};

void init_iodata(struct iodata *dat) {
  dat->entries = 0;
  dat->allocated = 32;
  dat->iov = (struct iovec *)calloc(dat->allocated, sizeof(struct iovec));
  if (!dat->iov) {
    fprintf(stderr, "alloc failure\n");
    exit(1);
  }
}

void enter_iodata(struct iodata *dat, char *line, size_t len) {
  while (dat->entries >= dat->allocated) {
    dat->allocated += 32;
    dat->iov = (struct iovec *)realloc(dat->iov, dat->allocated);
    if (!dat->iov) {
      fprintf(stderr, "realloc failure\n");
      exit(1);
    }
  }
  dat->iov[dat->entries].iov_base = line;
  dat->iov[dat->entries++].iov_len = len;
}

void free_iodata(struct iodata *dat) {
  for (size_t i = 0; i < dat->entries; ++i) {
    struct iovec *v = &dat->iov[i];
    free(v->iov_base);
  }
  free(dat->iov);
}

int iov_compare(const void *lptr, const void *rptr) {
  const char *lhs = ((const struct iovec *)lptr)->iov_base;
  const size_t nlhs = ((const struct iovec *)lptr)->iov_len;
  const char *rhs = ((const struct iovec *)rptr)->iov_base;
  const size_t nrhs = ((const struct iovec *)rptr)->iov_len;

  const size_t prefix_len = nlhs < nrhs ? nlhs : nrhs;
  for (size_t i = 0; i < prefix_len; ++i) {
    if (lhs[i] < rhs[i]) {
      return -1;
    } else if (lhs[i] > rhs[i]) {
      return 1;
    }
  }
  if (nlhs == nrhs) {
    return 0;
  } else if (nlhs < nrhs) {
    return -1;
  } else {
    return 1;
  }
}

int main() {
  struct iodata inp;
  init_iodata(&inp);

  char *header = (char *)calloc(32, sizeof(char));
  if (!header) {
    fprintf(stderr, "alloc failure\n");
    exit(1);
  }
  strncpy(header, ">> sorted input <<\n", 20);
  enter_iodata(&inp, header, strlen(header));

  while (true) {
    size_t buflen = 64;
    char *buf = (char *)calloc(buflen, sizeof(char));
    ssize_t nread = getline(&buf, &buflen, stdin);
    if (nread == -1) {
      fprintf(stderr, "getline failed, errno = %d\n", errno);
      exit(1);
    }
    if (nread == 1 && buf[0] == '\n') {
      break;
    }
    enter_iodata(&inp, buf, nread);
  } 

  qsort(inp.iov, inp.entries, sizeof(struct iovec), iov_compare);
  ssize_t nwrote = writev(STDOUT_FILENO, inp.iov, inp.entries);
  if (nwrote == -1) {
    fprintf(stderr, "writev syscall failed, errno = %d\n", errno);
  }

  free_iodata(&inp);
  return 0;
}
