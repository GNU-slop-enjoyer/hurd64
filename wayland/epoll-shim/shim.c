/* epoll-shim for the Hurd: epoll, timerfd, signalfd and eventfd, which
 * glibc on the Hurd does not have, on top of poll() and socketpairs.
 *
 * Every object is a real descriptor, so close(), dup() and poll() on it do
 * what callers expect: an epoll instance or a timerfd is the read end of a
 * socketpair, the state lives in a table indexed by that descriptor.
 *
 * Timers need no thread: epoll_wait() sleeps no longer than until the next
 * armed timer and, when one expires, writes an 8-byte expiration count
 * into its socket, which makes the timerfd readable. A timerfd therefore
 * only fires while its program waits in epoll_wait(), which is how
 * libwayland and foot use them.
 *
 * Edge triggering (EPOLLET) is treated as level triggering.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "include/sys/epoll.h"
#include "include/sys/eventfd.h"
#include "include/sys/signalfd.h"
#include "include/sys/timerfd.h"

#define MAX_FD 4096

#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME CLOCK_MONOTONIC	/* the Hurd has no suspend to count */
#endif

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* a socketpair: [0] is handed out, [1] stays here for writing */
static int make_pair(int flags, int out[2])
{
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, out) < 0)
		return -1;
	if (out[0] >= MAX_FD) {
		close(out[0]);
		close(out[1]);
		errno = EMFILE;
		return -1;
	}
	fcntl(out[1], F_SETFD, FD_CLOEXEC);
	fcntl(out[1], F_SETFL, O_NONBLOCK);
	if (flags & O_CLOEXEC)
		fcntl(out[0], F_SETFD, FD_CLOEXEC);
	if (flags & O_NONBLOCK)
		fcntl(out[0], F_SETFL, O_NONBLOCK);
	return 0;
}

/* timerfd */

struct timer {
	int fd, wfd;
	clockid_t clock;
	int armed;
	struct timespec next, interval;
};
static struct timer *timers[MAX_FD];

static int64_t ns_of(struct timespec t)
{
	return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static struct timespec ts_of(int64_t ns)
{
	return (struct timespec){ ns / 1000000000, ns % 1000000000 };
}

static int64_t now_ns(clockid_t c)
{
	struct timespec t;

	clock_gettime(c, &t);
	return ns_of(t);
}

int timerfd_create(int clockid, int flags)
{
	int p[2];

	if (clockid != CLOCK_MONOTONIC && clockid != CLOCK_REALTIME
	    && clockid != CLOCK_BOOTTIME) {
		errno = EINVAL;
		return -1;
	}
	if (make_pair(flags, p) < 0)
		return -1;
	struct timer *t = calloc(1, sizeof *t);
	if (!t) {
		close(p[0]);
		close(p[1]);
		errno = ENOMEM;
		return -1;
	}
	t->fd = p[0];
	t->wfd = p[1];
	t->clock = clockid == CLOCK_BOOTTIME ? CLOCK_MONOTONIC : clockid;
	pthread_mutex_lock(&lock);
	if (timers[p[0]]) {		/* left over from a closed timerfd */
		close(timers[p[0]]->wfd);
		free(timers[p[0]]);
	}
	timers[p[0]] = t;
	pthread_mutex_unlock(&lock);
	return p[0];
}

static struct timer *timer_of(int fd)
{
	return fd >= 0 && fd < MAX_FD ? timers[fd] : NULL;
}

/* drop expirations that were not read yet: setting a timer resets them */
static void drain(int fd)
{
	char buf[64];
	int fl = fcntl(fd, F_GETFL);

	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	while (read(fd, buf, sizeof buf) > 0)
		;
	fcntl(fd, F_SETFL, fl);
}

int timerfd_settime(int fd, int flags, const struct itimerspec *nv,
		    struct itimerspec *ov)
{
	pthread_mutex_lock(&lock);
	struct timer *t = timer_of(fd);
	if (!t) {
		pthread_mutex_unlock(&lock);
		errno = EBADF;
		return -1;
	}
	int64_t now = now_ns(t->clock);
	if (ov) {
		ov->it_interval = t->interval;
		ov->it_value = t->armed ? ts_of(ns_of(t->next) > now ? ns_of(t->next) - now : 1)
					: (struct timespec){ 0, 0 };
	}
	t->interval = nv->it_interval;
	t->armed = nv->it_value.tv_sec || nv->it_value.tv_nsec;
	if (t->armed)
		t->next = (flags & TFD_TIMER_ABSTIME) ? nv->it_value
						      : ts_of(now + ns_of(nv->it_value));
	pthread_mutex_unlock(&lock);
	drain(fd);
	return 0;
}

int timerfd_gettime(int fd, struct itimerspec *cur)
{
	pthread_mutex_lock(&lock);
	struct timer *t = timer_of(fd);
	if (!t) {
		pthread_mutex_unlock(&lock);
		errno = EBADF;
		return -1;
	}
	int64_t now = now_ns(t->clock);
	cur->it_interval = t->interval;
	cur->it_value = t->armed ? ts_of(ns_of(t->next) > now ? ns_of(t->next) - now : 1)
				 : (struct timespec){ 0, 0 };
	pthread_mutex_unlock(&lock);
	return 0;
}

/* Fire what has expired; return ms until the next armed timer, or -1.
   Called with the lock held. */
static int service_timers(void)
{
	int64_t soonest = -1;

	for (int i = 0; i < MAX_FD; i++) {
		struct timer *t = timers[i];
		if (!t || !t->armed)
			continue;
		int64_t now = now_ns(t->clock), at = ns_of(t->next);
		if (at <= now) {
			uint64_t n = 1;
			int64_t iv = ns_of(t->interval);
			if (iv > 0) {
				n += (now - at) / iv;
				t->next = ts_of(at + (int64_t)n * iv);
			} else {
				t->armed = 0;
			}
			if (send(t->wfd, &n, sizeof n, MSG_NOSIGNAL) < 0 && errno == EPIPE) {
				/* the timerfd was closed */
				close(t->wfd);
				free(t);
				timers[i] = NULL;
				continue;
			}
			if (!t->armed)
				continue;
			at = ns_of(t->next);
		}
		int64_t left = (at - now + 999999) / 1000000;
		if (soonest < 0 || left < soonest)
			soonest = left;
	}
	return soonest > INT32_MAX ? INT32_MAX : (int)soonest;
}

/* signalfd */

static int sig_wfd[NSIG];	/* write end per signal, -1 if none */
static pthread_once_t sig_once = PTHREAD_ONCE_INIT;

static void sig_init(void)
{
	for (int i = 0; i < NSIG; i++)
		sig_wfd[i] = -1;
}

static void sig_handler(int sig, siginfo_t *si, void *ctx)
{
	struct signalfd_siginfo s = { 0 };
	int e = errno;

	(void)ctx;
	s.ssi_signo = sig;
	if (si) {
		s.ssi_code = si->si_code;
		s.ssi_pid = si->si_pid;
		s.ssi_uid = si->si_uid;
		s.ssi_status = si->si_status;
	}
	if (sig > 0 && sig < NSIG && sig_wfd[sig] >= 0)
		send(sig_wfd[sig], &s, sizeof s, MSG_NOSIGNAL | MSG_DONTWAIT);
	errno = e;
}

int signalfd(int fd, const sigset_t *mask, int flags)
{
	int p[2];

	pthread_once(&sig_once, sig_init);
	if (fd != -1) {
		/* changing the mask of an existing signalfd: not needed so far */
		errno = EINVAL;
		return -1;
	}
	if (make_pair(flags, p) < 0)
		return -1;
	sigset_t unblock;
	sigemptyset(&unblock);
	for (int s = 1; s < NSIG; s++) {
		if (!sigismember(mask, s) || s == SIGKILL || s == SIGSTOP)
			continue;
		sig_wfd[s] = p[1];
		struct sigaction sa = { .sa_sigaction = sig_handler, .sa_flags = SA_SIGINFO | SA_RESTART };
		sigemptyset(&sa.sa_mask);
		sigaction(s, &sa, NULL);
		sigaddset(&unblock, s);
	}
	/* real signalfd wants the signals blocked; here the handler has to run */
	pthread_sigmask(SIG_UNBLOCK, &unblock, NULL);
	return p[0];
}

/* eventfd */

int eventfd(unsigned int initval, int flags)
{
	char path[64];
	static unsigned seq;
	int fd;

	/* a FIFO opened for reading and writing: what is written to the one
	   descriptor can be read back from it */
	for (int tries = 0;; tries++) {
		snprintf(path, sizeof path, "/tmp/.eventfd-%d-%u", getpid(),
			 __atomic_fetch_add(&seq, 1, __ATOMIC_RELAXED));
		if (mkfifo(path, 0600) == 0)
			break;
		if (errno != EEXIST || tries > 100)
			return -1;
	}
	fd = open(path, O_RDWR | O_NONBLOCK | (flags & O_CLOEXEC));
	unlink(path);
	if (fd < 0)
		return -1;
	if (!(flags & O_NONBLOCK))
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
	if (initval) {
		uint64_t v = initval;
		if (write(fd, &v, sizeof v) < 0) {
			close(fd);
			return -1;
		}
	}
	return fd;
}

int eventfd_read(int fd, eventfd_t *value)
{
	return read(fd, value, sizeof *value) == sizeof *value ? 0 : -1;
}

int eventfd_write(int fd, eventfd_t value)
{
	return write(fd, &value, sizeof value) == sizeof value ? 0 : -1;
}

/* epoll */

struct item {
	int fd;
	uint32_t events;
	epoll_data_t data;
	int disabled;		/* EPOLLONESHOT fired */
};

struct instance {
	int wfd;
	int n, cap;
	struct item *items;
};
static struct instance *instances[MAX_FD];

int epoll_create1(int flags)
{
	int p[2];

	if (flags & ~EPOLL_CLOEXEC) {
		errno = EINVAL;
		return -1;
	}
	if (make_pair(flags, p) < 0)
		return -1;
	struct instance *in = calloc(1, sizeof *in);
	if (!in) {
		close(p[0]);
		close(p[1]);
		errno = ENOMEM;
		return -1;
	}
	in->wfd = p[1];
	pthread_mutex_lock(&lock);
	if (instances[p[0]]) {		/* left over from a closed epoll fd */
		close(instances[p[0]]->wfd);
		free(instances[p[0]]->items);
		free(instances[p[0]]);
	}
	instances[p[0]] = in;
	pthread_mutex_unlock(&lock);
	return p[0];
}

int epoll_create(int size)
{
	if (size <= 0) {
		errno = EINVAL;
		return -1;
	}
	return epoll_create1(0);
}

static struct instance *instance_of(int epfd)
{
	return epfd >= 0 && epfd < MAX_FD ? instances[epfd] : NULL;
}

static int find(struct instance *in, int fd)
{
	for (int i = 0; i < in->n; i++)
		if (in->items[i].fd == fd)
			return i;
	return -1;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *ev)
{
	int r = 0;

	if (fcntl(fd, F_GETFD) < 0) {
		errno = EBADF;
		return -1;
	}
	if (fd == epfd) {
		errno = EINVAL;
		return -1;
	}
	pthread_mutex_lock(&lock);
	struct instance *in = instance_of(epfd);
	if (!in) {
		errno = EINVAL;
		r = -1;
		goto out;
	}
	int i = find(in, fd);
	switch (op) {
	case EPOLL_CTL_ADD:
		if (i >= 0) {
			errno = EEXIST;
			r = -1;
			break;
		}
		if (in->n == in->cap) {
			int cap = in->cap ? in->cap * 2 : 16;
			struct item *it = realloc(in->items, cap * sizeof *it);
			if (!it) {
				errno = ENOMEM;
				r = -1;
				break;
			}
			in->items = it;
			in->cap = cap;
		}
		in->items[in->n++] = (struct item){ fd, ev->events, ev->data, 0 };
		break;
	case EPOLL_CTL_MOD:
		if (i < 0) {
			errno = ENOENT;
			r = -1;
			break;
		}
		in->items[i].events = ev->events;
		in->items[i].data = ev->data;
		in->items[i].disabled = 0;
		break;
	case EPOLL_CTL_DEL:
		if (i < 0) {
			errno = ENOENT;
			r = -1;
			break;
		}
		in->items[i] = in->items[--in->n];
		break;
	default:
		errno = EINVAL;
		r = -1;
	}
out:
	pthread_mutex_unlock(&lock);
	return r;
}

static short to_poll(uint32_t e)
{
	short p = 0;

	if (e & (EPOLLIN | EPOLLRDNORM))
		p |= POLLIN;
	if (e & EPOLLPRI)
		p |= POLLPRI;
	if (e & (EPOLLOUT | EPOLLWRNORM))
		p |= POLLOUT;
	return p;
}

static uint32_t from_poll(short p, uint32_t wanted)
{
	uint32_t e = 0;

	if (p & POLLIN)
		e |= EPOLLIN;
	if (p & POLLPRI)
		e |= EPOLLPRI;
	if (p & POLLOUT)
		e |= EPOLLOUT;
	if (p & POLLERR)
		e |= EPOLLERR;
	if (p & POLLHUP)
		e |= EPOLLHUP | (wanted & EPOLLRDHUP);
	return e & (wanted | EPOLLERR | EPOLLHUP);
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
		const sigset_t *sigmask)
{
	if (maxevents <= 0) {
		errno = EINVAL;
		return -1;
	}
	int64_t deadline = timeout >= 0 ? now_ns(CLOCK_MONOTONIC) + (int64_t)timeout * 1000000 : -1;

	for (;;) {
		pthread_mutex_lock(&lock);
		struct instance *in = instance_of(epfd);
		if (!in) {
			pthread_mutex_unlock(&lock);
			errno = EBADF;
			return -1;
		}
		int next_timer = service_timers();
		int n = in->n;
		struct pollfd *pfd = malloc((n ? n : 1) * sizeof *pfd);
		struct item *copy = malloc((n ? n : 1) * sizeof *copy);
		if (!pfd || !copy) {
			pthread_mutex_unlock(&lock);
			free(pfd);
			free(copy);
			errno = ENOMEM;
			return -1;
		}
		memcpy(copy, in->items, n * sizeof *copy);
		pthread_mutex_unlock(&lock);
		for (int i = 0; i < n; i++)
			pfd[i] = (struct pollfd){ copy[i].disabled ? -1 : copy[i].fd,
						  to_poll(copy[i].events), 0 };

		int wait = -1;
		if (deadline >= 0) {
			int64_t left = (deadline - now_ns(CLOCK_MONOTONIC) + 999999) / 1000000;
			wait = left > 0 ? (left > INT32_MAX ? INT32_MAX : (int)left) : 0;
		}
		if (next_timer >= 0 && (wait < 0 || next_timer < wait))
			wait = next_timer;

		sigset_t old;
		if (sigmask)
			pthread_sigmask(SIG_SETMASK, sigmask, &old);
		int r = poll(pfd, n, wait);
		int err = errno;
		if (sigmask)
			pthread_sigmask(SIG_SETMASK, &old, NULL);
		if (r < 0) {
			free(pfd);
			free(copy);
			errno = err;
			return -1;
		}

		int got = 0;
		pthread_mutex_lock(&lock);
		in = instance_of(epfd);
		for (int i = 0; in && i < n && got < maxevents; i++) {
			if (!pfd[i].revents)
				continue;
			int j = find(in, copy[i].fd);
			if (j < 0)
				continue;	/* removed meanwhile */
			if (pfd[i].revents & POLLNVAL) {
				/* closed: the kernel drops closed fds from an epoll set */
				in->items[j] = in->items[--in->n];
				continue;
			}
			uint32_t e = from_poll(pfd[i].revents, in->items[j].events);
			if (!e)
				continue;
			events[got].events = e;
			events[got].data = in->items[j].data;
			got++;
			if (in->items[j].events & EPOLLONESHOT)
				in->items[j].disabled = 1;
		}
		pthread_mutex_unlock(&lock);
		free(pfd);
		free(copy);
		if (got)
			return got;
		if (deadline >= 0 && now_ns(CLOCK_MONOTONIC) >= deadline)
			return 0;
		/* woken for a timer (or by an fd that was dropped): go round */
	}
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	return epoll_pwait(epfd, events, maxevents, timeout, NULL);
}
