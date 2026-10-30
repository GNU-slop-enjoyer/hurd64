/* hurdwm keys; see keys.h. */
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include "../../utils/tuxkbd-events.h"
#include "keys.h"

/* USB HID usage -> Linux key code (from Linux drivers/hid/usbhid/usbkbd.c) */
static const unsigned char hid_to_linux[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 43, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117,183,184,185,186,187,188,189,190,
	191,192,193,194,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,121,  0, 89, 93,124, 92, 94, 95,  0,  0,  0,
	122,123, 90, 91, 85,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140
};

#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_LEFTMETA 125
#define KEY_ENTER 28

static int tuxkbd = 1;		/* events from tuxkbd, else bytes */
static int tty_fd = -1;
static struct termios saved;

int keys_open(const char *path)
{
	if (!path) {
		struct sockaddr_un a = { .sun_family = AF_UNIX };
		int s = socket(AF_UNIX, SOCK_STREAM, 0);
		strncpy(a.sun_path, TUXKBD_SOCKET, sizeof a.sun_path - 1);
		if (s < 0 || connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
			if (s >= 0)
				close(s);
			return -1;
		}
		return s;
	}
	tuxkbd = 0;
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd >= 0 && isatty(fd) && tcgetattr(fd, &saved) == 0) {
		struct termios t = saved;
		cfmakeraw(&t);
		tcsetattr(fd, TCSANOW, &t);
		tty_fd = fd;
	}
	return fd;
}

void keys_close(void)
{
	if (tty_fd >= 0)
		tcsetattr(tty_fd, TCSANOW, &saved);
	tty_fd = -1;
}

/* a typed character as Linux key code (US layout) and whether it is shifted */
static int ascii_key(unsigned char c, int *shift)
{
	static const char plain[] = "1234567890-=qwertyuiop[]asdfghjkl;'`\\zxcvbnm,./";
	static const char shifted[] = "!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"~|ZXCVBNM<>?";
	static const unsigned char codes[] = {
		2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
		30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 43,
		44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
	};
	const char *p;

	*shift = 0;
	if (c == '\r' || c == '\n') return KEY_ENTER;
	if (c == ' ') return 57;
	if (c == '\t') return 15;
	if (c == 0x7f || c == '\b') return 14;
	if (c == 0x1b) return 1;
	if ((p = strchr(plain, c)) && c)
		return codes[p - plain];
	if ((p = strchr(shifted, c)) && c) {
		*shift = 1;
		return codes[p - shifted];
	}
	return 0;
}

static void tap(const struct input_fns *f, void *data, int code, int shift, int super, int ctrl)
{
	if (super) f->key(data, KEY_LEFTMETA, true);
	if (ctrl) f->key(data, KEY_LEFTCTRL, true);
	if (shift) f->key(data, KEY_LEFTSHIFT, true);
	f->key(data, code, true);
	f->key(data, code, false);
	if (shift) f->key(data, KEY_LEFTSHIFT, false);
	if (ctrl) f->key(data, KEY_LEFTCTRL, false);
	if (super) f->key(data, KEY_LEFTMETA, false);
}

int keys_read(int fd, const struct input_fns *f, void *data)
{
	if (tuxkbd) {
		struct tuxkbd_event ev[32];
		ssize_t r = read(fd, ev, sizeof ev);
		if (r <= 0)
			return -1;
		int dx = 0, dy = 0;
		for (size_t i = 0; i < (size_t)r / sizeof ev[0]; i++) {
			switch (ev[i].type) {
			case TUXKBD_KEY:
				if (ev[i].code < 256 && hid_to_linux[ev[i].code])
					f->key(data, hid_to_linux[ev[i].code], ev[i].value);
				break;
			case TUXKBD_MOTION:
				/* gather x and y of one report into one move */
				if (ev[i].code == 0)
					dx += ev[i].value;
				else if (ev[i].code == 1)
					dy += ev[i].value;
				else
					f->wheel(data, ev[i].value);
				break;
			case TUXKBD_BUTTON:
				if (dx || dy)
					f->motion(data, dx, dy);
				dx = dy = 0;
				f->button(data, ev[i].code, ev[i].value);
				break;
			}
		}
		if (dx || dy)
			f->motion(data, dx, dy);
		return 0;
	}

	/* bytes; Ctrl+] then a key means Super+key, 'n' standing for Enter */
	static int prefix;
	unsigned char buf[256];
	ssize_t r = read(fd, buf, sizeof buf);
	if (r == 0 && tty_fd < 0)
		return -1;
	for (ssize_t i = 0; i < r; i++) {
		unsigned char c = buf[i];
		int shift, code;
		if (c == 0x1d && !prefix) {
			prefix = 1;
			continue;
		}
		if (prefix) {
			prefix = 0;
			code = c == 'n' ? KEY_ENTER : ascii_key(c, &shift);
			if (c == 'n')
				shift = 0;
			if (code)
				tap(f, data, code, shift, 1, 0);
			continue;
		}
		if (c >= 1 && c <= 26 && c != '\r' && c != '\n' && c != '\t' && c != '\b') {
			/* Ctrl+letter */
			code = ascii_key('a' + c - 1, &shift);
			tap(f, data, code, 0, 0, 1);
			continue;
		}
		code = ascii_key(c, &shift);
		if (code)
			tap(f, data, code, shift, 0, 0);
	}
	return 0;
}
