/* tuxkbd: a login on the kernel console, typed on a USB keyboard.
 *
 * GNU Mach only reads PS/2 keyboards. This runs a rump kernel with NetBSD's
 * xHCI/EHCI/OHCI/UHCI, USB and ukbd drivers in-process, reads key events
 * from its /dev/wskbd, and feeds them as bytes (US layout) into a pseudo
 * terminal that runs /bin/login. Whatever the terminal prints goes to
 * /dev/console, which on UEFI machines is GNU Mach's framebuffer console.
 *
 *   tuxkbd [-c console] [-l login]
 *
 * Keys typed on a PS/2 keyboard or a serial line reach the console itself and
 * are passed on as well. Run as root, once, from inittab instead of the
 * console getty.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/rumpdefs.h>

/* from NetBSD dev/wscons/wsconsio.h */
struct wscons_event {
	unsigned int type;
	int value;
	struct { int64_t tv_sec; long tv_nsec; } time;
};
#define WSCONS_EVENT_KEY_UP	1
#define WSCONS_EVENT_KEY_DOWN	2

/* USB HID usage codes (ukbd passes them through as key codes) */
enum {
	K_ENTER = 0x28, K_ESC, K_BACKSPACE, K_TAB, K_SPACE,
	K_CAPS = 0x39, K_RIGHT = 0x4f, K_LEFT, K_DOWN, K_UP,
	K_INSERT = 0x49, K_HOME, K_PGUP, K_DELETE, K_END, K_PGDN,
	K_LCTRL = 0xe0, K_LSHIFT, K_LALT, K_LGUI, K_RCTRL, K_RSHIFT, K_RALT, K_RGUI,
};

/* 0x1e..0x38: plain and shifted */
static const char row_plain[] = "1234567890\r\x1b\x7f\t -=[]\\#;'`,./";
static const char row_shift[] = "!@#$%^&*()\r\x1b\x7f\t _+{}|~:\"~<>?";

static int master = -1;
static const char *console = "/dev/console", *login_prog = "/bin/login";

static void put(const char *s, size_t n)
{
	while (n) {
		ssize_t w = write(master, s, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return;
		s += w;
		n -= w;
	}
}

/* one key press to the bytes a terminal expects */
static void key(int code, int shift, int ctrl, int alt, int caps)
{
	char buf[8];
	size_t n = 0;
	const char *seq = NULL;

	if (code >= 0x04 && code <= 0x1d) {		/* a..z */
		char c = 'a' + code - 0x04;
		if (shift != caps)
			c -= 'a' - 'A';
		if (ctrl)
			c &= 0x1f;
		buf[n++] = c;
	} else if (code >= 0x1e && code <= 0x38) {
		char c = (shift ? row_shift : row_plain)[code - 0x1e];
		if (ctrl && c == ' ')
			c = 0;
		buf[n++] = c;
	} else {
		switch (code) {
		case K_UP:	seq = "\x1b[A"; break;
		case K_DOWN:	seq = "\x1b[B"; break;
		case K_RIGHT:	seq = "\x1b[C"; break;
		case K_LEFT:	seq = "\x1b[D"; break;
		case K_HOME:	seq = "\x1b[H"; break;
		case K_END:	seq = "\x1b[F"; break;
		case K_INSERT:	seq = "\x1b[2~"; break;
		case K_DELETE:	seq = "\x1b[3~"; break;
		case K_PGUP:	seq = "\x1b[5~"; break;
		case K_PGDN:	seq = "\x1b[6~"; break;
		default:	return;
		}
	}
	if (alt)
		put("\x1b", 1);
	if (seq)
		put(seq, strlen(seq));
	else
		put(buf, n);
}

static void *keyboard(void *arg)
{
	(void)arg;
	int fd = -1;

	/* the keyboard may attach a moment after the USB bus is up */
	for (int i = 0; i < 100 && fd < 0; i++) {
		fd = rump_sys_open("/dev/wskbd", RUMP_O_RDONLY);
		if (fd < 0)
			usleep(100000);
	}
	if (fd < 0) {
		dprintf(2, "tuxkbd: no USB keyboard (/dev/wskbd: %d)\n", errno);
		return NULL;
	}

	int shift = 0, ctrl = 0, alt = 0, caps = 0;
	struct wscons_event ev[16];
	for (;;) {
		ssize_t r = rump_sys_read(fd, ev, sizeof ev);
		if (r <= 0) {
			usleep(10000);
			continue;
		}
		for (size_t i = 0; i < (size_t)r / sizeof ev[0]; i++) {
			int down = ev[i].type == WSCONS_EVENT_KEY_DOWN;
			if (ev[i].type != WSCONS_EVENT_KEY_DOWN && ev[i].type != WSCONS_EVENT_KEY_UP)
				continue;
			switch (ev[i].value) {
			case K_LSHIFT: case K_RSHIFT: shift = down; continue;
			case K_LCTRL: case K_RCTRL: ctrl = down; continue;
			case K_LALT: case K_RALT: alt = down; continue;
			case K_CAPS: if (down) caps = !caps; continue;
			}
			if (down)
				key(ev[i].value, shift, ctrl, alt, caps);
		}
	}
}

/* PS/2 or serial keys still arrive on the console itself: pass them on too */
static void *console_keys(void *arg)
{
	int in = *(int *)arg;
	char buf[64];

	for (;;) {
		ssize_t r = read(in, buf, sizeof buf);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0) {
			usleep(100000);
			continue;
		}
		put(buf, r);
	}
	return NULL;
}

/* everything the terminal prints goes to the console */
static void *screen(void *arg)
{
	int out = *(int *)arg;
	char buf[1024];

	for (;;) {
		ssize_t r = read(master, buf, sizeof buf);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0) {
			usleep(100000);
			continue;
		}
		for (ssize_t off = 0; off < r;) {
			ssize_t w = write(out, buf + off, r - off);
			if (w <= 0)
				break;
			off += w;
		}
	}
	return NULL;
}

static pid_t start_login(void)
{
	pid_t pid = fork();

	if (pid != 0)
		return pid;
	setsid();
	int slave = open(ptsname(master), O_RDWR);
	if (slave < 0)
		_exit(1);
	ioctl(slave, TIOCSCTTY, 0);
	dup2(slave, 0);
	dup2(slave, 1);
	dup2(slave, 2);
	if (slave > 2)
		close(slave);
	close(master);
	setenv("TERM", "linux", 1);
	execl(login_prog, login_prog, (char *)NULL);
	_exit(127);
}

int main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "c:l:")) != -1) {
		if (opt == 'c')
			console = optarg;
		else if (opt == 'l')
			login_prog = optarg;
		else {
			fprintf(stderr, "usage: tuxkbd [-c console] [-l login]\n");
			return 2;
		}
	}

	int out = open(console, O_RDWR | O_NOCTTY);
	if (out < 0) {
		perror(console);
		return 1;
	}
	/* the pty does echo and line editing; the console just passes bytes */
	struct termios t;
	if (tcgetattr(out, &t) == 0) {
		cfmakeraw(&t);
		t.c_oflag |= OPOST | ONLCR;
		tcsetattr(out, TCSANOW, &t);
	}
	if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0 || grantpt(master) || unlockpt(master)) {
		perror("tuxkbd: pty");
		return 1;
	}
	rump_boot_sethowto(RUMP_AB_QUIET);
	if (rump_init() != 0) {
		dprintf(out, "tuxkbd: rump_init failed\r\n");
		return 1;
	}

	pthread_t kt, st, ct;
	pthread_create(&kt, NULL, keyboard, NULL);
	pthread_create(&st, NULL, screen, &out);
	pthread_create(&ct, NULL, console_keys, &out);

	/* keep a login on the console; start a new one when it exits */
	for (;;) {
		pid_t pid = start_login();
		if (pid < 0) {
			sleep(1);
			continue;
		}
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
			;
		sleep(1);
	}
}
