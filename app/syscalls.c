/*
 * Newlib syscall stubs. console output (printf) goes to SEGGER RTT up
 * buffer 0; _sbrk serves the small libc heap reserved in the linker script.
 */
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "SEGGER_RTT.h"

int _write(int fd, const char *ptr, int len);
int _read(int fd, char *ptr, int len);

extern char __HeapBase;   /* from linker script */
extern char __HeapLimit;
static char *heap_end = NULL;

int _write(int fd, const char *ptr, int len)
{
    (void)fd;
    SEGGER_RTT_Write(0, ptr, (unsigned)len);
    return len;
}

int _read(int fd, char *ptr, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        int c = SEGGER_RTT_GetKey();   /* RTT down channel 0 (blocking-free) */
        if (c < 0) {
            return i;
        }
        ptr[i] = (char)c;
    }
    return len;
}

int _close(int fd)   { (void)fd; errno = ENODEV; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)  { (void)fd; return 1; }
int _lseek(int fd, int off, int dir) { (void)fd; (void)off; (void)dir; return 0; }
off_t _lseek64(int fd, off_t off, int dir) { (void)fd; (void)off; (void)dir; return 0; }

int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status) { (void)status; while (1) {} }

caddr_t _sbrk(int incr)
{
    if (heap_end == NULL) {
        heap_end = &__HeapBase;
    }
    char *prev = heap_end;
    if (heap_end + incr > &__HeapLimit) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }
    heap_end += incr;
    return (caddr_t)prev;
}
