/*
 * syscalls.c — Minimal syscall stubs for bare-metal THEJAS32
 *
 * These stubs prevent libc from calling ecall (RISC-V syscall)
 * which would trap and crash on bare metal with no OS.
 */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Heap management (matches VSD BSP sbrk) */
void *_sbrk(ptrdiff_t incr) {
  extern char _end[];
  extern char _heap_end[];
  static char *curbrk = 0;
  if (curbrk == 0)
    curbrk = _end;

  if ((curbrk + incr < _end) || (curbrk + incr > _heap_end))
    return (void *)-1;

  char *prev = curbrk;
  curbrk += incr;
  return prev;
}

/* File I/O stubs — just return success/error */
int _write(int fd, const void *buf, int len) {
  (void)fd;
  (void)buf;
  return len; /* Pretend we wrote it */
}

int _read(int fd, void *buf, int len) {
  (void)fd;
  (void)buf;
  (void)len;
  return 0;
}

int _close(int fd) {
  (void)fd;
  return -1;
}

int _lseek(int fd, int offset, int whence) {
  (void)fd;
  (void)offset;
  (void)whence;
  return 0;
}

int _fstat(int fd, void *buf) {
  (void)fd;
  (void)buf;
  return 0;
}

int _isatty(int fd) {
  (void)fd;
  return 1;
}

/* Process stubs */
void _exit(int status) {
  (void)status;
  while (1) {
    __asm__ volatile("nop");
  }
}

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  return -1;
}

int _getpid(void) { return 1; }
