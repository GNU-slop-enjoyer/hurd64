/* test for epoll-shim: a pipe, a repeating timerfd, a signalfd and an
 * eventfd in one epoll set */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

int main(void)
{
	int ep = epoll_create1(EPOLL_CLOEXEC), p[2], fails = 0;
	pipe(p);
	int tf = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	struct itimerspec its = { { 0, 50000000 }, { 0, 100000000 } };	/* 100 ms, then every 50 */
	timerfd_settime(tf, 0, &its, NULL);
	sigset_t m;
	sigemptyset(&m);
	sigaddset(&m, SIGUSR1);
	sigprocmask(SIG_BLOCK, &m, NULL);
	int sf = signalfd(-1, &m, SFD_CLOEXEC);
	int ef = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	int fds[4] = { p[0], tf, sf, ef };
	for (int i = 0; i < 4; i++) {
		struct epoll_event ev = { .events = EPOLLIN, .data.u32 = i };
		if (epoll_ctl(ep, EPOLL_CTL_ADD, fds[i], &ev) < 0)
			printf("epoll_ctl %d failed\n", i), fails++;
	}
	write(p[1], "x", 1);
	kill(getpid(), SIGUSR1);
	eventfd_write(ef, 7);

	int seen[4] = { 0 }, ticks = 0;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	while (ticks < 5) {
		struct epoll_event evs[8];
		int n = epoll_wait(ep, evs, 8, 2000);
		if (n <= 0) {
			printf("epoll_wait returned %d\n", n);
			fails++;
			break;
		}
		for (int i = 0; i < n; i++) {
			int k = evs[i].data.u32;
			char buf[256];
			if (k == 1) {
				uint64_t c;
				if (read(tf, &c, 8) == 8)
					ticks += c;
			} else if (k == 2) {
				struct signalfd_siginfo si;
				if (read(sf, &si, sizeof si) == sizeof si && si.ssi_signo == SIGUSR1)
					seen[2]++;
			} else if (k == 3) {
				eventfd_t v;
				if (eventfd_read(ef, &v) == 0 && v == 7)
					seen[3]++;
			} else if (read(fds[k], buf, sizeof buf) > 0)
				seen[k]++;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
	printf("pipe %d signal %d eventfd %d timer ticks %d in %ld ms (want about 300)\n",
	       seen[0], seen[2], seen[3], ticks, ms);
	if (!seen[0] || !seen[2] || !seen[3] || ms < 250 || ms > 600)
		fails++;
	printf(fails ? "FAIL\n" : "ok\n");
	return fails != 0;
}
