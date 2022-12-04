#define _GNU_SOURCE
#include <sys/types.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <linux/futex.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <string.h>

#define NR_WAKE_UP 1
#define PAGE_SIZE 4096

static int futex(atomic_int *uaddr, int op, uint32_t val, struct timespec *ts,
          uint32_t *uaddr2, uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, ts, uaddr2, val3);
}

static int futex_wake(atomic_int *uaddr, int nr) {
  return futex(uaddr, FUTEX_WAKE, nr, NULL, NULL, 0);
}

static int futex_wait(atomic_int *uaddr, int val) {
  return futex(uaddr, FUTEX_WAIT, val, NULL, NULL, 0);
}

void sem_init(atomic_int *sem, unsigned int initval) {
  atomic_init(sem, initval);
}

void sem_incr(atomic_int *sem) {
  bool succ = false;
  while (!succ) {
    int cur = atomic_load(sem);
    succ = atomic_compare_exchange_strong(sem, &cur, cur + 1);
    cur = atomic_load(sem);
    if (cur > 0) {
      futex_wake(sem, NR_WAKE_UP);
    }
  }
}

void sem_decr(atomic_int *sem) {
  bool succ = false;
  while (!succ) {
    int cur = atomic_load(sem);
    if (cur > 0) {
      succ = atomic_compare_exchange_strong(sem, &cur, cur - 1);
    } else {
      futex_wait(sem, 0);
    }
  }
}

#define BOUNDED_BUFFER_LEN 3

typedef struct {
  atomic_int slots;
  atomic_int elements;
  atomic_int lock; 
  uint32_t read_idx;
  uint32_t write_idx;
  void *data[BOUNDED_BUFFER_LEN];
} BoundedBuffer;

void bbuf_init(BoundedBuffer *buffer) {
  sem_init(&buffer->slots, BOUNDED_BUFFER_LEN);
  sem_init(&buffer->elements, 0);
  sem_init(&buffer->lock, 1);
  buffer->write_idx = 0;
  buffer->read_idx = 0;
}

void bbuf_put(BoundedBuffer *buffer, void *data) {
  sem_decr(&buffer->slots);
  sem_decr(&buffer->lock);
  buffer->data[buffer->write_idx] = data;
  buffer->write_idx = (buffer->write_idx + 1) % BOUNDED_BUFFER_LEN;
  sem_incr(&buffer->lock);
  sem_incr(&buffer->elements);
}

void *bbuf_get(BoundedBuffer *buffer) {
  void *ret = NULL;
  sem_decr(&buffer->elements);
  sem_decr(&buffer->lock);
  ret = buffer->data[buffer->read_idx];
  buffer->read_idx = (buffer->read_idx + 1) % BOUNDED_BUFFER_LEN;
  sem_incr(&buffer->lock);
  sem_incr(&buffer->slots);
  return ret;
}

int main(int argc, char **argv) {
  uint8_t *smem = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);  
  if (smem == MAP_FAILED) {
    fprintf(stderr, "failed to create shared memory area\n");
    fprintf(stderr, "mmap errno is %d\n", errno);
    return 1;
  }

  pid_t child = fork();
  if (child == -1) {
    fprintf(stderr, "fork failed with errno %d\n", errno);
    return 1;
  }

  atomic_int *wait_on_buff_init = (void *)&smem[0];
  sem_init(wait_on_buff_init, 0);
  BoundedBuffer *bbuf = (void *)&smem[sizeof(atomic_int)];

  if (child != 0) {
    // -> parent process
    sem_decr(wait_on_buff_init); // wait on bounded buffer
    while (true) {
      char *str = (char *)bbuf_get(bbuf);
      printf("received: %s\n", str);
    }
  } else {
    char *data[] = {
      "hello", "world", "from", "the", "child", "process"
    };
    sleep(1);
    bbuf_init(bbuf);
    sem_incr(wait_on_buff_init);

    int next = 0;
    while (next < 6) {
      bbuf_put(bbuf, (void *)data[next++]);
    }
  }
  return 0;
}
