/* timerfd for the Hurd (epoll-shim). The timers are serviced inside
 * epoll_wait(), so a timerfd only fires while its program waits in
 * epoll_wait(); read() it after epoll says it is readable. */
#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H
#include <time.h>
#include <fcntl.h>

#define TFD_CLOEXEC		O_CLOEXEC
#define TFD_NONBLOCK		O_NONBLOCK
#define TFD_TIMER_ABSTIME	1
#define TFD_TIMER_CANCEL_ON_SET	2

#ifdef __cplusplus
extern "C" {
#endif
int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
		    struct itimerspec *old_value);
int timerfd_gettime(int fd, struct itimerspec *curr_value);
#ifdef __cplusplus
}
#endif
#endif
