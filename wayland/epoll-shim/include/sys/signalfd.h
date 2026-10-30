/* signalfd for the Hurd (epoll-shim): a handler writes each signal into
 * the descriptor. */
#ifndef _SYS_SIGNALFD_H
#define _SYS_SIGNALFD_H
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>

#define SFD_CLOEXEC	O_CLOEXEC
#define SFD_NONBLOCK	O_NONBLOCK

struct signalfd_siginfo {
	uint32_t ssi_signo;
	int32_t ssi_errno;
	int32_t ssi_code;
	uint32_t ssi_pid;
	uint32_t ssi_uid;
	int32_t ssi_fd;
	uint32_t ssi_tid;
	uint32_t ssi_band;
	uint32_t ssi_overrun;
	uint32_t ssi_trapno;
	int32_t ssi_status;
	int32_t ssi_int;
	uint64_t ssi_ptr;
	uint64_t ssi_utime;
	uint64_t ssi_stime;
	uint64_t ssi_addr;
	uint16_t ssi_addr_lsb;
	uint8_t pad[46];
};

#ifdef __cplusplus
extern "C" {
#endif
int signalfd(int fd, const sigset_t *mask, int flags);
#ifdef __cplusplus
}
#endif
#endif
