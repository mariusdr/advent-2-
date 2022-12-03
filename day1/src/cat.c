#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BUFLEN 512

static size_t file_len(const int fd) {
  off_t off = lseek(fd, 0, SEEK_END);
  if (off < 0) {
    fprintf(stderr, "failed to compute file length with errno %d", errno);
    return 0;
  }
  if (lseek(fd, 0, SEEK_SET) < 0) {
    fprintf(stderr, "could not reset file offset, errno %d", errno);
    return 0;
  }
  return off;
}

static int read_chunk_to_stdout(const int fd, size_t *const ndone) {
  if (lseek(fd, *ndone, SEEK_SET) < 0) {
    fprintf(stderr, "could not set file to position %zu, errno %d\n", *ndone, errno);
    return 1;
  }

  static char buf[BUFLEN];
  memset(buf, '\0', BUFLEN);
  ssize_t nread = read(fd, buf, BUFLEN);
  if (nread < 0) {
    switch (errno) {
      case EBADF:
        fprintf(stderr, "read failed due to invalid file descriptor\n");
        break;
      case EFAULT:
        fprintf(stderr, "read buffer is outside of accesible address space\n");
        break;
      case EISDIR:
        fprintf(stderr, "tried to read from directory\n");
        break;
      default:
        fprintf(stderr, "read failed with errno %d", errno);
        break;
    }
    return 1;
  }
  *ndone += nread;
  printf("%s", buf);
  return 0;
}

static int read_content(const char *const filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    switch (errno) {
      case EACCES:
        fprintf(stderr, "accessing file %s is not allowed\n", filepath);
        break;
      case EISDIR:
        fprintf(stderr, "%s is a directory\n", filepath);
        break;
      case ENFILE:
        fprintf(stderr, "system limit on open files has been reached\n");
        break;
      default:
        fprintf(stderr, "opening %s failed with errno %d\n", filepath, errno);
        break;
    }
    return 1;
  }

  size_t flen = file_len(fd);
  size_t nread = 0;
  while (nread < flen) {
    if (read_chunk_to_stdout(fd, &nread) != 0) {
      break; // error during read
    }
  }

  if (close(fd) < 0) {
    switch (errno) {
      case EBADF:
        fprintf(stderr, "closing an invalid file descriptor\n");
        break;
      default:
        fprintf(stderr, "closing %s failed with errno %d\n", filepath, errno);
        break;
    }
    return 1;
  }
  return 0;
}

static const char *next_file_path(int argc, char **argv) {
  static int cur = 1;
  if (cur < argc) {
    return argv[cur++];
  }
  return NULL;
}

int main(int argc, char **argv) {
  const char *fpath = NULL;
  while ((fpath = next_file_path(argc, argv))) {
    read_content(fpath);
  }
  return 0;
}
