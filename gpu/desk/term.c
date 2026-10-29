/* Terminal emulation for tuxdesk; see term.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "term.h"

const uint32_t term_palette[16] = {
	0x1c1e20, 0xc0392b, 0x4e9a06, 0xc4a000, 0x3465a4, 0x9b59b6, 0x06989a, 0xd3d7cf,
	0x555753, 0xef2929, 0x8ae234, 0xfce94f, 0x729fcf, 0xad7fa8, 0x34e2e2, 0xeeeeec,
};
#define DEF_FG 7
#define DEF_BG 0

enum { S_TEXT, S_ESC, S_CSI, S_OSC, S_CHARSET };

static struct cell blank(struct term *t)
{
	return (struct cell){ ' ', t->fg, t->bg, 0 };
}

static void clear_cells(struct term *t, int from, int to)
{
	for (int i = from; i < to; i++)
		t->cells[i] = blank(t);
	t->dirty = 1;
}

/* move lines [top, bottom] up by n (n < 0: down) */
static void scroll(struct term *t, int top, int bottom, int n)
{
	int lines = bottom - top + 1;

	if (n >= lines || -n >= lines) {
		clear_cells(t, top * t->cols, (bottom + 1) * t->cols);
		return;
	}
	if (n > 0) {
		memmove(&t->cells[top * t->cols], &t->cells[(top + n) * t->cols],
			(lines - n) * t->cols * sizeof *t->cells);
		clear_cells(t, (bottom - n + 1) * t->cols, (bottom + 1) * t->cols);
	} else if (n < 0) {
		n = -n;
		memmove(&t->cells[(top + n) * t->cols], &t->cells[top * t->cols],
			(lines - n) * t->cols * sizeof *t->cells);
		clear_cells(t, top * t->cols, (top + n) * t->cols);
	}
}

static void newline(struct term *t)
{
	if (t->cy == t->bottom)
		scroll(t, t->top, t->bottom, 1);
	else if (t->cy < t->rows - 1)
		t->cy++;
}

static void clamp(struct term *t)
{
	if (t->cx < 0) t->cx = 0;
	if (t->cx >= t->cols) t->cx = t->cols - 1;
	if (t->cy < 0) t->cy = 0;
	if (t->cy >= t->rows) t->cy = t->rows - 1;
	t->wrap_next = 0;
}

static void put(struct term *t, uint8_t ch)
{
	if (t->wrap_next) {
		t->cx = 0;
		newline(t);
		t->wrap_next = 0;
	}
	struct cell *c = &t->cells[t->cy * t->cols + t->cx];
	c->ch = ch;
	c->fg = t->fg;
	c->bg = t->bg;
	c->attr = t->attr;
	if (t->cx == t->cols - 1)
		t->wrap_next = 1;
	else
		t->cx++;
	t->dirty = 1;
}

/* a few Unicode characters the font has (code page 437) */
static uint8_t cp437(uint32_t u)
{
	static const struct { uint16_t u; uint8_t c; } map[] = {
		{ 0x2500, 0xc4 }, { 0x2502, 0xb3 }, { 0x250c, 0xda }, { 0x2510, 0xbf },
		{ 0x2514, 0xc0 }, { 0x2518, 0xd9 }, { 0x251c, 0xc3 }, { 0x2524, 0xb4 },
		{ 0x252c, 0xc2 }, { 0x2534, 0xc1 }, { 0x253c, 0xc5 }, { 0x2550, 0xcd },
		{ 0x2551, 0xba }, { 0x2588, 0xdb }, { 0x2591, 0xb0 }, { 0x2592, 0xb1 },
		{ 0x2593, 0xb2 }, { 0x00b7, 0xfa }, { 0x00b0, 0xf8 }, { 0x2022, 0x07 },
		{ 0x00e4, 0x84 }, { 0x00f6, 0x94 }, { 0x00fc, 0x81 }, { 0x00c4, 0x8e },
		{ 0x00d6, 0x99 }, { 0x00dc, 0x9a }, { 0x00df, 0xe1 }, { 0x00e9, 0x82 },
		{ 0x2192, 0x1a }, { 0x2190, 0x1b }, { 0x2191, 0x18 }, { 0x2193, 0x19 },
	};
	if (u < 0x80)
		return u;
	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
		if (map[i].u == u)
			return map[i].c;
	return '?';
}

static int param(struct term *t, int i, int def)
{
	return i < t->nparam && t->param[i] > 0 ? t->param[i] : def;
}

static void sgr(struct term *t)
{
	if (t->nparam == 0)
		t->nparam = 1, t->param[0] = 0;
	for (int i = 0; i < t->nparam; i++) {
		int p = t->param[i];

		if (p == 0) {
			t->fg = DEF_FG, t->bg = DEF_BG, t->attr = 0;
		} else if (p == 1) {
			t->attr |= ATTR_BOLD;
		} else if (p == 22) {
			t->attr &= ~ATTR_BOLD;
		} else if (p == 7) {
			t->attr |= ATTR_REVERSE;
		} else if (p == 27) {
			t->attr &= ~ATTR_REVERSE;
		} else if (p >= 30 && p <= 37) {
			t->fg = p - 30;
		} else if (p == 39) {
			t->fg = DEF_FG;
		} else if (p >= 40 && p <= 47) {
			t->bg = p - 40;
		} else if (p == 49) {
			t->bg = DEF_BG;
		} else if (p >= 90 && p <= 97) {
			t->fg = p - 90 + 8;
		} else if (p >= 100 && p <= 107) {
			t->bg = p - 100 + 8;
		} else if ((p == 38 || p == 48) && i + 2 < t->nparam && t->param[i + 1] == 5) {
			/* 256 colours: keep the first 16, others to the nearest basic one */
			int c = t->param[i + 2];
			if (c >= 16)
				c = c < 232 ? 7 : (c < 244 ? 8 : 15);
			if (p == 38) t->fg = c; else t->bg = c;
			i += 2;
		} else if ((p == 38 || p == 48) && i + 4 < t->nparam && t->param[i + 1] == 2) {
			i += 4;		/* true colour: not here */
		}
	}
}

static void csi(struct term *t, char final)
{
	int n = param(t, 0, 1);
	struct cell *row = &t->cells[t->cy * t->cols];

	switch (final) {
	case 'A': t->cy -= n; clamp(t); break;
	case 'B': case 'e': t->cy += n; clamp(t); break;
	case 'C': case 'a': t->cx += n; clamp(t); break;
	case 'D': t->cx -= n; clamp(t); break;
	case 'E': t->cx = 0; t->cy += n; clamp(t); break;
	case 'F': t->cx = 0; t->cy -= n; clamp(t); break;
	case 'G': case '`': t->cx = n - 1; clamp(t); break;
	case 'd': t->cy = n - 1; clamp(t); break;
	case 'H': case 'f':
		t->cy = param(t, 0, 1) - 1;
		t->cx = param(t, 1, 1) - 1;
		clamp(t);
		break;
	case 'J': {
		int at = t->cy * t->cols + t->cx, mode = t->nparam ? t->param[0] : 0;
		if (mode == 0) clear_cells(t, at, t->cols * t->rows);
		else if (mode == 1) clear_cells(t, 0, at + 1);
		else clear_cells(t, 0, t->cols * t->rows);
		break;
	}
	case 'K': {
		int base = t->cy * t->cols, mode = t->nparam ? t->param[0] : 0;
		if (mode == 0) clear_cells(t, base + t->cx, base + t->cols);
		else if (mode == 1) clear_cells(t, base, base + t->cx + 1);
		else clear_cells(t, base, base + t->cols);
		break;
	}
	case 'X':
		clear_cells(t, t->cy * t->cols + t->cx,
			    t->cy * t->cols + (t->cx + n < t->cols ? t->cx + n : t->cols));
		break;
	case 'P':		/* delete characters */
		if (n > t->cols - t->cx) n = t->cols - t->cx;
		memmove(&row[t->cx], &row[t->cx + n], (t->cols - t->cx - n) * sizeof *row);
		clear_cells(t, t->cy * t->cols + t->cols - n, (t->cy + 1) * t->cols);
		break;
	case '@':		/* insert blanks */
		if (n > t->cols - t->cx) n = t->cols - t->cx;
		memmove(&row[t->cx + n], &row[t->cx], (t->cols - t->cx - n) * sizeof *row);
		clear_cells(t, t->cy * t->cols + t->cx, t->cy * t->cols + t->cx + n);
		break;
	case 'L':
		if (t->cy >= t->top && t->cy <= t->bottom)
			scroll(t, t->cy, t->bottom, -n);
		break;
	case 'M':
		if (t->cy >= t->top && t->cy <= t->bottom)
			scroll(t, t->cy, t->bottom, n);
		break;
	case 'S': scroll(t, t->top, t->bottom, n); break;
	case 'T': scroll(t, t->top, t->bottom, -n); break;
	case 'm': sgr(t); break;
	case 'r':
		t->top = param(t, 0, 1) - 1;
		t->bottom = param(t, 1, t->rows) - 1;
		if (t->top < 0 || t->bottom >= t->rows || t->top >= t->bottom)
			t->top = 0, t->bottom = t->rows - 1;
		t->cx = t->cy = 0;
		break;
	case 's': t->saved_cx = t->cx; t->saved_cy = t->cy; break;
	case 'u': t->cx = t->saved_cx; t->cy = t->saved_cy; clamp(t); break;
	case 'h': case 'l':
		if (t->priv && param(t, 0, 0) == 25)
			t->cursor_on = final == 'h';
		break;
	case 'n':		/* cursor position report */
		if (param(t, 0, 0) == 6) {
			char buf[32];
			int len = snprintf(buf, sizeof buf, "\033[%d;%dR", t->cy + 1, t->cx + 1);
			term_write(t, buf, len);
		}
		break;
	}
	t->dirty = 1;
}

static void feed(struct term *t, uint8_t b)
{
	switch (t->state) {
	case S_ESC:
		t->state = S_TEXT;
		switch (b) {
		case '[':
			t->state = S_CSI;
			t->nparam = 0;
			t->priv = 0;
			memset(t->param, 0, sizeof t->param);
			return;
		case ']': t->state = S_OSC; t->title[0] = 0; return;
		case '(': case ')': t->state = S_CHARSET; return;
		case '7': t->saved_cx = t->cx; t->saved_cy = t->cy; return;
		case '8': t->cx = t->saved_cx; t->cy = t->saved_cy; clamp(t); return;
		case 'D': newline(t); return;
		case 'E': t->cx = 0; newline(t); return;
		case 'M':
			if (t->cy == t->top)
				scroll(t, t->top, t->bottom, -1);
			else if (t->cy > 0)
				t->cy--;
			return;
		case 'c':
			t->fg = DEF_FG, t->bg = DEF_BG, t->attr = 0;
			t->top = 0, t->bottom = t->rows - 1;
			t->cx = t->cy = 0;
			clear_cells(t, 0, t->cols * t->rows);
			return;
		}
		return;
	case S_CHARSET:
		t->state = S_TEXT;
		return;
	case S_OSC:		/* ESC ] 0 ; title BEL */
		if (b == 7 || b == 0x9c || b == 0x1b) {
			t->state = b == 0x1b ? S_ESC : S_TEXT;
			char *semi = strchr(t->title, ';');
			if (semi)
				memmove(t->title, semi + 1, strlen(semi));
			t->dirty = 1;
		} else {
			size_t l = strlen(t->title);
			if (l + 1 < sizeof t->title) {
				t->title[l] = b;
				t->title[l + 1] = 0;
			}
		}
		return;
	case S_CSI:
		if (b >= '0' && b <= '9') {
			if (t->nparam == 0)
				t->nparam = 1;
			int *p = &t->param[t->nparam - 1];
			*p = *p * 10 + (b - '0');
		} else if (b == ';') {
			if (t->nparam == 0)
				t->nparam = 1;
			if (t->nparam < 16)
				t->nparam++;
		} else if (b == '?' || b == '>' || b == '=') {
			t->priv = 1;
		} else if (b >= 0x40 && b <= 0x7e) {
			t->state = S_TEXT;
			csi(t, b);
		} else if (b == 0x18 || b == 0x1a) {
			t->state = S_TEXT;
		}
		return;
	}

	/* S_TEXT */
	if (t->utf8_left) {
		if ((b & 0xc0) == 0x80) {
			t->utf8 = t->utf8 << 6 | (b & 0x3f);
			if (--t->utf8_left == 0)
				put(t, cp437(t->utf8));
			return;
		}
		t->utf8_left = 0;
		put(t, '?');
	}
	switch (b) {
	case 0x1b: t->state = S_ESC; return;
	case '\r': t->cx = 0; t->wrap_next = 0; return;
	case '\n': case 0x0b: case 0x0c: newline(t); t->wrap_next = 0; return;
	case '\b': if (t->cx > 0) t->cx--; t->wrap_next = 0; return;
	case '\t':
		t->cx = (t->cx + 8) & ~7;
		if (t->cx >= t->cols) t->cx = t->cols - 1;
		return;
	case 7: case 0: case 0x0e: case 0x0f: return;
	}
	if (b < 0x20 || b == 0x7f)
		return;
	if (b >= 0xc0 && b < 0xf8) {
		t->utf8_left = b >= 0xf0 ? 3 : b >= 0xe0 ? 2 : 1;
		t->utf8 = b & (0x3f >> t->utf8_left);
		return;
	}
	if (b >= 0x80) {
		put(t, '?');
		return;
	}
	put(t, b);
}

static void set_size(struct term *t)
{
	struct winsize ws = { .ws_row = t->rows, .ws_col = t->cols };

	ioctl(t->master, TIOCSWINSZ, &ws);
}

struct term *term_new(int cols, int rows, const char *shell)
{
	struct term *t = calloc(1, sizeof *t);

	if (!t)
		return NULL;
	t->cols = cols;
	t->rows = rows;
	t->cells = calloc((size_t)cols * rows, sizeof *t->cells);
	t->fg = DEF_FG;
	t->bg = DEF_BG;
	t->bottom = rows - 1;
	t->cursor_on = 1;
	clear_cells(t, 0, cols * rows);

	if (!shell)
		shell = getenv("SHELL");
	if (!shell || !*shell) {
		struct passwd *pw = getpwuid(getuid());
		shell = pw && pw->pw_shell && *pw->pw_shell ? pw->pw_shell : "/bin/sh";
	}
	snprintf(t->title, sizeof t->title, "%s", strrchr(shell, '/') ? strrchr(shell, '/') + 1 : shell);

	t->master = posix_openpt(O_RDWR | O_NOCTTY);
	if (t->master < 0 || grantpt(t->master) || unlockpt(t->master))
		goto fail;
	set_size(t);
	t->pid = fork();
	if (t->pid < 0)
		goto fail;
	if (t->pid == 0) {
		setsid();
		int s = open(ptsname(t->master), O_RDWR);
		if (s < 0)
			_exit(1);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		close(t->master);
		setenv("TERM", "linux", 1);
		/* a locale that is not installed only makes the shell complain */
		if (!setlocale(LC_ALL, "")) {
			unsetenv("LC_ALL");
			setenv("LANG", "C.UTF-8", 1);
		}
		signal(SIGCHLD, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		const char *home = getenv("HOME");
		if (home && chdir(home) < 0)
			_exit(1);
		execl(shell, shell, "-l", (char *)NULL);
		_exit(127);
	}
	fcntl(t->master, F_SETFL, O_NONBLOCK);
	return t;
fail:
	term_free(t);
	return NULL;
}

void term_free(struct term *t)
{
	if (!t)
		return;
	if (t->master >= 0)
		close(t->master);
	if (t->pid > 0)
		kill(t->pid, SIGHUP);
	free(t->cells);
	free(t);
}

int term_read(struct term *t)
{
	uint8_t buf[4096];

	for (;;) {
		ssize_t r = read(t->master, buf, sizeof buf);
		if (r > 0) {
			for (ssize_t i = 0; i < r; i++)
				feed(t, buf[i]);
			continue;
		}
		if (r < 0 && (errno == EAGAIN || errno == EINTR))
			return 0;
		return -1;	/* EIO or EOF: the shell has gone */
	}
}

void term_write(struct term *t, const char *buf, size_t n)
{
	while (n) {
		ssize_t w = write(t->master, buf, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return;
		buf += w;
		n -= w;
	}
}

void term_resize(struct term *t, int cols, int rows)
{
	struct cell *c = calloc((size_t)cols * rows, sizeof *c);

	if (!c)
		return;
	for (int i = 0; i < cols * rows; i++)
		c[i] = (struct cell){ ' ', DEF_FG, DEF_BG, 0 };
	/* keep the bottom of the old screen, where the prompt is */
	int skip = t->cy >= rows ? t->cy - rows + 1 : 0;
	for (int y = 0; y < rows && y + skip < t->rows; y++)
		for (int x = 0; x < cols && x < t->cols; x++)
			c[y * cols + x] = t->cells[(y + skip) * t->cols + x];
	free(t->cells);
	t->cells = c;
	t->cy -= skip;
	t->cols = cols;
	t->rows = rows;
	t->top = 0;
	t->bottom = rows - 1;
	clamp(t);
	set_size(t);
	t->dirty = 1;
}
