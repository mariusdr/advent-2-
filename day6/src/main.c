#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ucontext.h>
#include <errno.h>

int PAGE_SIZE;

volatile bool do_exit = false;

static void sigsegv_action(int signo, siginfo_t *info, void *ctx) {
  printf("SIGSEGV handler called at addr %p\n", info->si_addr);
  void *addr = info->si_addr;
  void *maddr = mmap(addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (maddr == MAP_FAILED) {
    printf("mmap failed with errno %d\n", errno);
  }
  printf("mmap'd new page for %p starting at %p\n", addr, maddr);
}

static void install_sigsegv_handler(void) {
  struct sigaction action;
  action.sa_sigaction = sigsegv_action;
  action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &action, NULL);
}

static void sigill_action(int signo, siginfo_t *info, void *ctx) {
  ucontext_t *uctx =(ucontext_t *)ctx;
  greg_t ip = uctx->uc_mcontext.gregs[REG_RIP];
  printf("sigill at instruction ptr %llu ..\n", ip);
  uctx->uc_mcontext.gregs[REG_RIP] += 4;
  printf("incremented ip by 4 bytes \n");
}

static void install_sigill_handler(void) {
  struct sigaction action;
  action.sa_sigaction = sigill_action;
  action.sa_flags = SA_SIGINFO;
  sigaction(SIGILL, &action, NULL);
}

static void sigint_action(int signo, siginfo_t *info, void *ctx) {
  printf("sigint occured, set do_exit\n");
  do_exit = true;
}

static void install_sigint_handler(void) {
  struct sigaction action;
  action.sa_sigaction = sigint_action;
  action.sa_flags = SA_SIGINFO;
  sigaction(SIGINT, &action, NULL);
}

static void dump_pmap(void) {
  char cmd[256];
  snprintf(cmd, 256, "pmap %d", getpid());
  printf("---- system(\"%s\"):\n", cmd);
  system(cmd);
}

int main(void) {
#define INVALID_OPCODE_32_BIT() __asm__("ud2; ud2;")
  install_sigsegv_handler();
  install_sigill_handler();
  install_sigint_handler();

  PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
  uint32_t *addr = (uint32_t *)0xdeadbeef;
  *addr = 23;


  INVALID_OPCODE_32_BIT();

  while (!do_exit) {
    sleep(1);
    addr += 22559;
    *addr = 42;
    INVALID_OPCODE_32_BIT();
  }

  dump_pmap();
  return EXIT_SUCCESS;
}
