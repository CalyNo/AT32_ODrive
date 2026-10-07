#include "board.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

static uint8_t s_heap[8192];
static size_t s_heap_used;

int _write(int file, char *ptr, int len)
{
  (void)file;
  if ((ptr == NULL) || (len <= 0))
  {
    return 0;
  }
  board_uart_write((const uint8_t *)ptr, (uint32_t)len);
  return len;
}

void *_sbrk(int incr)
{
  void *result;

  if ((incr < 0) || ((s_heap_used + (size_t)incr) > sizeof(s_heap)))
  {
    return (void *)-1;
  }

  result = &s_heap[s_heap_used];
  s_heap_used += (size_t)incr;
  return result;
}

int _close(int file)
{
  (void)file;
  return -1;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  if (st != NULL)
  {
    st->st_mode = S_IFCHR;
  }
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _read(int file, char *ptr, int len)
{
  (void)file;
  (void)ptr;
  (void)len;
  return 0;
}

void _exit(int status)
{
  (void)status;
  while (1)
  {
  }
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  return -1;
}

int _getpid(void)
{
  return 1;
}
