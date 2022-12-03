#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <fcntl.h>

#define STACK_SIZE 4096

char message[128];

int child_exec(void *args) {
  memset(message, '\0', sizeof(message));
  strcpy(message, "hello from child!");
  printf("child: my pid is %d\n", getpid());
  printf("child: my tid is %d\n", gettid());
  printf("child: my uid is %d\n", getuid());
  printf("child: my message is '%s'\n", message);
  return 0;
}

// exec in own uid namespace, map local uid's to parent's uid and become root
// in local namespace afterwards
int child_exec2(void *args) {
  int fd = open("/proc/self/uid_map", O_RDWR);
  write(fd, (char *)args, strlen((char *)args));
  close(fd);

  if (setuid(0) == -1) {
    printf("setuid failed with errno %d\n", errno);
    return 1;
  }
  memset(message, '\0', sizeof(message));
  strcpy(message, "hello from child!");
  printf("child: my pid is %d\n", getpid());
  printf("child: my tid is %d\n", gettid());
  printf("child: my uid is %d\n", getuid());
  printf("child: my message is '%s'\n", message);
  return 0;
}

// child and parent have different pids and different address spaces
pid_t cl_fork() {
  char *childstack = (char *)calloc(STACK_SIZE, sizeof(char));
  if (!childstack) {
    fprintf(stderr, "failed to allocate child stack\n");
    return -1;
  }
  int flags = 0;
  pid_t pid = clone(child_exec, childstack + STACK_SIZE, flags | SIGCHLD, NULL);
  if (pid == -1) {
    fprintf(stderr, "clone failed with errno %d\n", errno);
    return -1;
  }
  return pid;
}

pid_t cl_fork_new_uid_namespace() {
  char *childstack = (char *)calloc(STACK_SIZE, sizeof(char));
  if (!childstack) {
    fprintf(stderr, "failed to allocate child stack\n");
    return -1;
  }
  char *uid_map = calloc(32, sizeof(char));
  sprintf(uid_map, "0 %d 1\n", getuid());

  int flags = CLONE_NEWUSER;
  pid_t pid = clone(child_exec2, childstack + STACK_SIZE, flags | SIGCHLD, (void *)uid_map);
  if (pid == -1) {
    fprintf(stderr, "clone failed with errno %d\n", errno);
    return -1;
  }
  free(uid_map);
  return pid;
}

// child and parent have different pids but the same address space
pid_t cl_chimera() {
  char *childstack = (char *)calloc(STACK_SIZE, sizeof(char));
  if (!childstack) {
    fprintf(stderr, "failed to allocate child stack\n");
    return -1;
  }
  int flags = CLONE_VM;
  pid_t pid = clone(child_exec, childstack + STACK_SIZE, flags | SIGCHLD, NULL);
  if (pid == -1) {
    fprintf(stderr, "clone failed with errno %d\n", errno);
    return -1;
  }
  return pid;
}

// spaws a new thread in the same "process" ~ "thread group" as the caller
pid_t cl_thread() {
  char *childstack = (char *)calloc(STACK_SIZE, sizeof(char));
  if (!childstack) {
    fprintf(stderr, "failed to allocate child stack\n");
    return -1;
  }
  int flags = CLONE_VM | CLONE_THREAD | CLONE_SIGHAND;
  pid_t pid = clone(child_exec, childstack + STACK_SIZE, flags, NULL);
  if (pid == -1) {
    fprintf(stderr, "clone failed with errno %d\n", errno);
    return -1;
  }
  return pid;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    return 1;
  }
  memset(message, '\0', sizeof(message));
  strcpy(message, "hello from parent!");
  
  if (strncmp(argv[1], "fork", 5) == 0) {
    cl_fork();
    printf("parent: my pid is %d\n", getpid());
    printf("parent: my tid is %d\n", gettid());
    printf("parent: my uid is %d\n", getuid());
    int status;
    if (wait(&status) == -1) {
      fprintf(stderr, "error on wait, errno %d\n", errno);
      return 1;
    }
  } else if (strncmp(argv[1], "fork2", 6) == 0) {
    cl_fork_new_uid_namespace();
    printf("parent: my pid is %d\n", getpid());
    printf("parent: my tid is %d\n", gettid());
    printf("parent: my uid is %d\n", getuid());
    int status;
    if (wait(&status) == -1) {
      fprintf(stderr, "error on wait, errno %d\n", errno);
      return 1;
    }
  } else if (strncmp(argv[1], "chimera", 8) == 0) {
    cl_chimera();
    printf("parent: my pid is %d\n", getpid());
    printf("parent: my tid is %d\n", gettid());
    printf("parent: my uid is %d\n", getuid());
    int status;
    if (wait(&status) == -1) {
      fprintf(stderr, "error on wait, errno %d\n", errno);
      return 1;
    }
  } else if (strncmp(argv[1], "thread", 7) == 0) {
    cl_thread();
    printf("parent: my pid is %d\n", getpid());
    printf("parent: my tid is %d\n", gettid());
  } else {
    return 1;
  }
  printf("parent: my message is '%s'\n", message);
  return 0;
}