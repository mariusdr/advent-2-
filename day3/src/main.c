#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

// section(..) pushes variables in a seperate named ELF section
#define persistent __attribute__((section("persistent")))

// aligns variables to the mmu's page size
#define PAGE_SIZE 4096
#define page_aligned __attribute__((aligned(PAGE_SIZE)))

// .section persistent
page_aligned persistent int pstart;
persistent unsigned int counter = 0;
page_aligned persistent int pend;

int setup_persistent(const char *fname) {
  int rc = 0;
  int fd = open(fname, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "failed to open file %s with errno %d\n", fname, errno);
    return -1;
  }
  void *addr = mmap(&pstart, &pend - &pstart, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    fprintf(stderr, "mmap failed with errno %d\n", errno);
    rc = -1;
    goto exit;
  }
  if (addr != &pstart) {
    fprintf(stderr, "non-fixed mapping of persistent section\n");
    rc = -1; 
    goto exit;
  }
exit:
  if (close(fd) < 0) {
    fprintf(stderr, "failed to close file %s with errno %d\n", fname, errno);
    rc = -1;
  }
  return rc;
}

int main(int argc, char **argv) {
  if (setup_persistent("mmap.persistent") == -1) {
    fprintf(stderr, "setup_persistent failed\n");
    return 1;
  }
  printf("persistent %p - %p\n", &pstart, &pend);
  printf("counter addr: %p\n", &counter);
  printf("count = %d\n", counter++);
  return 0;
}
