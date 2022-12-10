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

struct proc {
  char *cmd;
  pid_t pid;
  int stdin;
  int stdout;
  char last;
};

static int nprocs;
static struct proc *procs;

static int start_proc(struct proc *proc) {
  char *stdbuf_cmd;
  asprintf(&stdbuf_cmd, "stdbuf -oL %s", proc->cmd);
  char *argv[] = {"sh", "-c", stdbuf_cmd, 0};

  int stdin[2];
  int stdout[2];
  if (pipe2(stdin, O_CLOEXEC)) {
    return -1;
  }
  if (pipe2(stdout, O_CLOEXEC)) {
    return -1;
  }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, stdin[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&fa, stdout[1], STDOUT_FILENO);

  extern char **environ;
  int err;
  if (!(err = posix_spawn(&proc->pid, "/bin/sh", &fa, 0, argv, environ))) {
    posix_spawn_file_actions_destroy(&fa);
    free(stdbuf_cmd);
    
    // within parent process, close pipe ends that are also
    // used in child
    proc->stdin = stdin[1];
    close(stdin[0]);
    proc->stdout = stdout[0];
    close(stdout[1]);
    return 0;
  } else {
    errno = err;
    free(stdbuf_cmd);
    return -1;
  }
}

#define BUFLEN 4096 

void close_procs() {
  for (int i = 0; i < nprocs; ++i) {
    if (procs[i].pid != 0) {
      close(procs[i].stdin);
    }
  }
}

void *stdin_thread(void *data) {
  char *buf = calloc(BUFLEN, sizeof(char));
  if (!buf) {
    fprintf(stderr, "malloc failed");
    exit(1);
  }

  while (true) {
    ssize_t len = read(STDIN_FILENO, buf, BUFLEN);
    if (len <= 0) {
      close_procs();
      return NULL;
    }

    for (int i = 0; i < nprocs; ++i) {
      if (procs[i].pid != 0) {
        write(procs[i].stdin, buf, len);
      }
    }
  }
}

ssize_t drain_proc(struct proc *proc, char *buf, size_t buflen) {
  int len = read(proc->stdout, buf, buflen - 1);
  if (len < 0) {
    fprintf(stderr, "read failed in drain_proc with errno %d", errno);
    exit(1);
  } else if (len == 0) {
    int state;
    waitpid(proc->pid, &state, 0);
    fprintf(stderr, "[%s] filter exited. exitcode=%d\n", proc->cmd, WEXITSTATUS(state));
    proc->pid = 0;
  }

  for (int i = 0; i < len; ++i) {
    if (proc->last == '\n') {
      printf("[%s] ", proc->cmd);
    }
    putchar(buf[i]);
    proc->last = buf[i];
  }
  return len;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "usage: %s [CMD] (<CMD-2>, <CMD-3> ...)\n", argv[0]);
    return -1;
  }
  nprocs = argc - 1;
  procs = calloc(nprocs, sizeof(struct proc));
  if (!procs) {
    fprintf(stderr, "calloc failed\n");
    return -1;
  }

  for (int i = 0; i < nprocs; ++i) {
    procs[i].cmd = argv[i + 1];
    procs[i].last = '\n';
    int rc = start_proc(&procs[i]);
    if (rc < 0) {
      fprintf(stderr, "start filter failed\n");
      return -1;
    }
    fprintf(stderr, "[%s] started filter as pid %d\n", procs[i].cmd, procs[i].pid);
  }

  pthread_t handle;
  int rc = pthread_create(&handle, NULL, stdin_thread, NULL);
  if (rc < 0) {
    fprintf(stderr, "failed to create stdin thread\n");
    return -1;
  }

  char *buf = (char *)calloc(BUFLEN, sizeof(char));
  if (!buf) {
    fprintf(stderr, "malloc failed");
    return -1;
  }

  while (true) {
    int nfds = 0;
    fd_set readfds;
    FD_ZERO(&readfds);
    for (int i = 0; i < nprocs; ++i) {
      if (procs[i].pid != 0) {
        FD_SET(procs[i].stdout, &readfds);
        if (nfds < procs[i].stdout) {
          nfds = procs[i].stdout;
        }
      }
    }

    if (nfds == 0) {
      break;
    }

    int rc = select(nfds + 1, &readfds, NULL, NULL, NULL);
    if (rc == -1) {
      fprintf(stderr, "select failed with errno %d", errno);
      return -1;
    }

    for (int i = 0; i < nprocs; ++i) {
      if (FD_ISSET(procs[i].stdout, &readfds)) {
        drain_proc(&procs[i], buf, BUFLEN);
      }
    }
  }

  return 0;
}
