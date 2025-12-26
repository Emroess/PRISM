#pragma GCC diagnostic ignored "-Wmissing-prototypes"

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#ifndef ENOSYS
#define ENOSYS 38	/* Function not implemented */
#endif

/*
 * Weak stubs for unimplemented syscalls to suppress linker warnings
 *
 * These are required to suppress linker errors when newlib expects
 * certain syscalls to be defined. Since this firmware does not use
 * standard I/O or process management, these stubs simply return
 * ENOSYS to indicate the functions are not implemented.
 */
__attribute__((weak)) int
_close(int fd)
{
	(void)fd;
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_fstat(int fd, struct stat *st)
{
	(void)fd;
	(void)st;
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_getpid(void)
{
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_isatty(int fd)
{
	(void)fd;
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_kill(int pid, int sig)
{
	(void)pid;
	(void)sig;
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_lseek(int fd, int ptr, int dir)
{
	(void)fd;
	(void)ptr;
	(void)dir;
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_read(int fd, char *ptr, int len)
{
	(void)fd;
	(void)ptr;
	(void)len;
	/* errno = ENOSYS; */
	return -1;
}

__attribute__((weak)) int
_write(int fd, char *ptr, int len)
{
	(void)fd;
	(void)ptr;
	(void)len;
	/* errno = ENOSYS; */
	return -1;
}