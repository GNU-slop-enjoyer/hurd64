/* Keys from tuxkbd (USB keyboard, see utils/tuxkbd-events.h) for tuxdesk:
 * HID usages to the bytes a terminal expects, US layout, like tuxkbd does
 * for the console. */
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../../utils/tuxkbd-events.h"
#include "input.h"

enum {
	K_ENTER = 0x28, K_ESC, K_BACKSPACE, K_TAB, K_SPACE,
	K_CAPS = 0x39, K_F1 = 0x3a, K_RIGHT = 0x4f, K_LEFT, K_DOWN, K_UP,
	K_INSERT = 0x49, K_HOME, K_PGUP, K_DELETE, K_END, K_PGDN,
	K_LCTRL = 0xe0, K_LSHIFT, K_LALT, K_LGUI, K_RCTRL, K_RSHIFT, K_RALT, K_RGUI,
};

/* 0x1e..0x38: plain and shifted */
static const char row_plain[] = "1234567890\r\x1b\x7f\t -=[]\\#;'`,./";
static const char row_shift[] = "!@#$%^&*()\r\x1b\x7f\t _+{}|~:\"~<>?";

int input_connect(void)
{
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

static int shift, ctrl, alt, caps, super;

/* one key press; Super+key becomes a tuxdesk command, the rest bytes */
static void press(int code, struct input_out *out)
{
	char buf[8];
	size_t n = 0;
	const char *seq = NULL;

	if (super) {
		static const struct { int code; int cmd; } map[] = {
			{ K_ENTER, 'n' }, { 0x11, 'n' }, { K_TAB, '\t' }, { 0x14, 'q' },
			{ 0x10, 'm' }, { 0x13, 'p' }, { K_ESC, 'x' },
			{ K_UP, 'A' }, { K_DOWN, 'B' }, { K_RIGHT, 'C' }, { K_LEFT, 'D' },
		};
		for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
			if (map[i].code == code)
				out->command(map[i].cmd);
		return;
	}
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
		else if (ctrl && (c == ']' || c == '}'))
			c = 0x1d;
		else if (ctrl && (c == '[' || c == '{'))
			c = 0x1b;
		else if (ctrl && (c == '\\' || c == '|'))
			c = 0x1c;
		buf[n++] = c;
	} else {
		switch (code) {
		case K_UP:	seq = "\x1b[A"; break;
		case K_DOWN:	seq = "\x1b[B"; break;
		case K_RIGHT:	seq = "\x1b[C"; break;
		case K_LEFT:	seq = "\x1b[D"; break;
		case K_HOME:	seq = "\x1b[1~"; break;
		case K_END:	seq = "\x1b[4~"; break;
		case K_INSERT:	seq = "\x1b[2~"; break;
		case K_DELETE:	seq = "\x1b[3~"; break;
		case K_PGUP:	seq = "\x1b[5~"; break;
		case K_PGDN:	seq = "\x1b[6~"; break;
		default:
			/* F1..F12, as the Linux console sends them */
			if (code >= K_F1 && code < K_F1 + 12) {
				static const char *f[12] = {
					"\x1b[[A", "\x1b[[B", "\x1b[[C", "\x1b[[D", "\x1b[[E",
					"\x1b[17~", "\x1b[18~", "\x1b[19~", "\x1b[20~",
					"\x1b[21~", "\x1b[23~", "\x1b[24~",
				};
				seq = f[code - K_F1];
				break;
			}
			return;
		}
	}
	if (alt)
		out->bytes("\x1b", 1);
	if (seq)
		out->bytes(seq, strlen(seq));
	else
		out->bytes(buf, n);
}

int input_read(int fd, struct input_out *out)
{
	struct tuxkbd_event ev[32];
	ssize_t r = read(fd, ev, sizeof ev);

	if (r <= 0)
		return -1;
	for (size_t i = 0; i < (size_t)r / sizeof ev[0]; i++) {
		if (ev[i].type != TUXKBD_KEY)
			continue;
		int down = ev[i].value;
		switch (ev[i].code) {
		case K_LSHIFT: case K_RSHIFT: shift = down; continue;
		case K_LCTRL: case K_RCTRL: ctrl = down; continue;
		case K_LALT: case K_RALT: alt = down; continue;
		case K_LGUI: case K_RGUI: super = down; continue;
		case K_CAPS: if (down) caps = !caps; continue;
		}
		if (down)
			press(ev[i].code, out);
	}
	return 0;
}
