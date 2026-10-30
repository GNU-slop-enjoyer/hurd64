/* eventfd for the Hurd (epoll-shim): each write is one 8-byte record,
 * each read takes one. Good enough for wakeups. */
#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H
#include <stdint.h>
#include <fcntl.h>

#define EFD_CLOEXEC	O_CLOEXEC
#define EFD_NONBLOCK	O_NONBLOCK
#define EFD_SEMAPHORE	1

typedef uint64_t eventfd_t;

#ifdef __cplusplus
extern "C" {
#endif
int eventfd(unsigned int initval, int flags);
int eventfd_read(int fd, eventfd_t *value);
int eventfd_write(int fd, eventfd_t value);
#ifdef __cplusplus
}
#endif
#endif
