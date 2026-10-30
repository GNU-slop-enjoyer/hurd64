/* hurd96kbd: a login on the kernel console, typed on a USB keyboard.
 *
 * GNU Mach only reads PS/2 keyboards. This runs a rump kernel with NetBSD's
 * xHCI/EHCI/OHCI/UHCI, USB and ukbd drivers in-process, reads key events
 * from its /dev/wskbd, and feeds them as bytes (US layout) into a pseudo
 * terminal that runs /bin/login. Whatever the terminal prints goes to
 * /dev/console, which on UEFI machines is GNU Mach's framebuffer console.
 *
 *   hurd96kbd [-c console] [-l login]
 *
 * A program can take the keyboard over (the desktop does): it connects to
 * /run/hurd96kbd.sock and gets key events, and USB mouse events, which have
 * nowhere else to go (hurd96kbd-events.h), until it hangs up.
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/rumpdefs.h>

#include "hurd96kbd-events.h"

/* from NetBSD dev/wscons/wsconsio.h */
struct wscons_event {
	unsigned int type;
	int value;
	struct { int64_t tv_sec; long tv_nsec; } time;
};
#define WSCONS_EVENT_KEY_UP	1
#define WSCONS_EVENT_KEY_DOWN	2
#define WSCONS_EVENT_MOUSE_UP		4	/* value: button, leftmost 0 */
#define WSCONS_EVENT_MOUSE_DOWN		5
#define WSCONS_EVENT_MOUSE_DELTA_X	6
#define WSCONS_EVENT_MOUSE_DELTA_Y	7	/* up is positive */
#define WSCONS_EVENT_MOUSE_DELTA_Z	10	/* wheel */

/* the rump kernel runs in this process; ask it for wskbd's real major */
extern int rumpns_devsw_name2chr(const char *, char *, size_t);
static const char *kbd_node = "/dev/wskbd";
static const char *mouse_node = "/dev/wsmouse";

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
/* a program that took over the keyboard (HURD96KBD_SOCKET), or -1 */
static int client = -1;
static pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *logf;				/* /var/log/hurd96kbd.log */

#define LOG(...) do { if (logf) { fprintf(logf, __VA_ARGS__); fflush(logf); } } while (0)
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

/* hand an event to the program that took over the keyboard; 0 if there is none */
static int to_client(int type, int code, int value)
{
	struct hurd96kbd_event e = { type, code, value };
	int sent = 0;

	pthread_mutex_lock(&client_lock);
	if (client >= 0) {
		if (write(client, &e, sizeof e) == sizeof e) {
			sent = 1;
		} else {
			LOG("client gone, keys back to the console\n");
			close(client);
			client = -1;
		}
	}
	pthread_mutex_unlock(&client_lock);
	return sent;
}

/* one program at a time may take over the keyboard; a new one replaces it */
static void *listener(void *arg)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	int s = socket(AF_UNIX, SOCK_STREAM, 0);

	(void)arg;
	strncpy(a.sun_path, HURD96KBD_SOCKET, sizeof a.sun_path - 1);
	unlink(HURD96KBD_SOCKET);
	if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 2) < 0) {
		LOG("no %s: error %d\n", HURD96KBD_SOCKET, errno);
		return NULL;
	}
	chmod(HURD96KBD_SOCKET, 0600);
	for (;;) {
		int c = accept(s, NULL, NULL);
		if (c < 0) {
			if (errno != EINTR)
				sleep(1);
			continue;
		}
		pthread_mutex_lock(&client_lock);
		if (client >= 0)
			close(client);
		client = c;
		pthread_mutex_unlock(&client_lock);
		LOG("keys go to a client now\n");
	}
}

static void *keyboard(void *arg)
{
	(void)arg;
	int fd = -1;

	/* USB discovery runs in the background and keyboards can be plugged in
	   later: keep trying until one shows up */
	LOG("waiting for a keyboard\n");
	for (int tries = 0; (fd = rump_sys_open(kbd_node, RUMP_O_RDONLY)) < 0; tries++) {
		if (tries == 0)
			LOG("open %s: error %d\n", kbd_node, errno);
		sleep(tries < 30 ? 1 : 5);
	}
	LOG("keyboard open (fd %d)\n", fd);

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
			if (to_client(HURD96KBD_KEY, ev[i].value, down))
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

/* USB mice: only a client (the desktop) gets their events */
static void *mouse(void *arg)
{
	int fd;

	(void)arg;
	for (int tries = 0; (fd = rump_sys_open(mouse_node, RUMP_O_RDONLY)) < 0; tries++)
		sleep(tries < 30 ? 1 : 5);
	LOG("mouse open (fd %d)\n", fd);

	struct wscons_event ev[32];
	for (;;) {
		ssize_t r = rump_sys_read(fd, ev, sizeof ev);
		if (r <= 0) {
			usleep(10000);
			continue;
		}
		for (size_t i = 0; i < (size_t)r / sizeof ev[0]; i++) {
			switch (ev[i].type) {
			case WSCONS_EVENT_MOUSE_DELTA_X:
				to_client(HURD96KBD_MOTION, 0, ev[i].value);
				break;
			case WSCONS_EVENT_MOUSE_DELTA_Y:
				to_client(HURD96KBD_MOTION, 1, -ev[i].value);
				break;
			case WSCONS_EVENT_MOUSE_DELTA_Z:
				to_client(HURD96KBD_MOTION, 2, ev[i].value);
				break;
			case WSCONS_EVENT_MOUSE_DOWN:
			case WSCONS_EVENT_MOUSE_UP:
				to_client(HURD96KBD_BUTTON, ev[i].value,
					  ev[i].type == WSCONS_EVENT_MOUSE_DOWN);
				break;
			}
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
			fprintf(stderr, "usage: hurd96kbd [-c console] [-l login]\n");
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
		perror("hurd96kbd: pty");
		return 1;
	}
	rump_boot_sethowto(RUMP_AB_SILENT);
	logf = fopen("/var/log/hurd96kbd.log", "a");
	LOG("hurd96kbd starting\n");
	if (rump_init() != 0) {
		dprintf(out, "hurd96kbd: rump_init failed\r\n");
		return 1;
	}

	/* /dev/wskbd from the wscons component can carry another driver's major
	   (open fails with ENXIO); make a node from the major wskbd really got */
	rump_schedule();
	int maj = rumpns_devsw_name2chr("wskbd", NULL, 0);
	rump_unschedule();
	LOG("wskbd major %d\n", maj);
	if (maj >= 0 && rump_sys_mknod("/dev/hurd96wskbd0", 0020000 | 0600, (uint64_t)maj << 8) == 0)
		kbd_node = "/dev/hurd96wskbd0";
	rump_schedule();
	maj = rumpns_devsw_name2chr("wsmouse", NULL, 0);
	rump_unschedule();
	LOG("wsmouse major %d\n", maj);
	if (maj >= 0 && rump_sys_mknod("/dev/hurd96wsmouse0", 0020000 | 0600, (uint64_t)maj << 8) == 0)
		mouse_node = "/dev/hurd96wsmouse0";

	pthread_t kt, st, ct;
	pthread_create(&kt, NULL, keyboard, NULL);
	pthread_create(&st, NULL, screen, &out);
	pthread_create(&ct, NULL, console_keys, &out);
	pthread_t lt;
	signal(SIGPIPE, SIG_IGN);
	pthread_create(&lt, NULL, listener, NULL);
	pthread_t mt;
	pthread_create(&mt, NULL, mouse, NULL);

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
