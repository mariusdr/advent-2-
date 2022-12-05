#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(*(arr)))

struct event_flag {
  int mask;
  char *name;
};

struct event_flag inotify_event_flags[] = {
    {IN_ACCESS, "access"},
    {IN_ATTRIB, "attrib"},
    {IN_CLOSE_WRITE, "close_write"},
    {IN_CLOSE_NOWRITE, "close_nowrite"},
    {IN_CREATE, "create"},
    {IN_DELETE, "delete"},
    {IN_DELETE_SELF, "delete_self"},
    {IN_MODIFY, "modify"},
    {IN_MOVE_SELF, "move_self"},
    {IN_MOVED_FROM, "move_from"},
    {IN_MOVED_TO, "moved_to"},
    {IN_OPEN, "open"},
    {IN_MOVE, "move"},
    {IN_CLOSE, "close"},
    {IN_MASK_ADD, "mask_add"},
    {IN_IGNORED, "ignored"},
    {IN_ISDIR, "directory"},
    {IN_UNMOUNT, "unmount"},
};

static const char *get_event_str(int mask) {
  size_t len = ARRAY_SIZE(inotify_event_flags);
  for (size_t i = 0; i < len; ++i) {
    if (inotify_event_flags[i].mask == mask) {
      return inotify_event_flags[i].name;
    }
  }
  return NULL;
}

static const int is_valid_event(int mask) {
  size_t len = ARRAY_SIZE(inotify_event_flags);
  for (size_t i = 0; i < len; ++i) {
    if (inotify_event_flags[i].mask == mask) {
      return 1;
    }
  }
  return 0;
}

static void print_event(const struct inotify_event *ev) {
  const char *evn = get_event_str(ev->mask);
  printf("event %d (%s) ", ev->mask, evn);
  if (ev->len > 0) {
    printf("%s\n", ev->name);
  } else {
    printf("\n");
  }
}

int main(void) {
  void *buffer = malloc(4096);
  if (!buffer) {
    fprintf(stderr, "malloc failed\n");
    return 1;
  }

  int inotfd = inotify_init();
  if (inotfd == -1) {
    fprintf(stderr, "inotify_init failed with errno %d\n", errno);
    return 1;
  }

  char *cwd = getcwd(NULL, 0);
  if (!cwd) {
    fprintf(stderr, "could not get current directory name\n");
    return 1;
  }
  
  inotify_add_watch(inotfd, cwd, IN_OPEN | IN_ACCESS | IN_CLOSE);
  while (true) {
    ssize_t len = read(inotfd, buffer, 4096);
    if (len == -1 && errno != EAGAIN) {
      fprintf(stderr, "read error with errno %d\n", errno);
      return 1;
    }
    if (len <= 0) {
      break;
    }
    unsigned char *ptr = buffer;
    while (ptr < (unsigned char*)buffer + len) {
      struct inotify_event *ev = (struct inotify_event *)ptr; 
      if (is_valid_event(ev->mask)) {
        print_event(ev);
      }
      ptr += sizeof(struct inotify_event) + ev->len; 
    }
  }

  if (close(inotfd) == -1) {
    fprintf(stderr, "close failed with errno %d\n", errno);
    return 1;
  }
  free(cwd);
  free(buffer);
  return 0;
}
