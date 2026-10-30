/* tuxdesk: a desktop for the Hurd, drawn through Linux's KMS in-process.
 *
 *   tuxdesk [-d driver] [-m MiB] [-k keyboard] [-l log] [kernel args...]
 *
 *   -d  DRM driver of the card: amdgpu, bochs-drm, ... With neither -d nor
 *       kernel args tuxdesk draws on the framebuffer the firmware set up.
 *   -m  RAM for Linux inside tuxdesk, MiB (default 512)
 *   -k  where keys come from (default: standard input)
 *   -l  Linux's messages (default /var/log/tuxdesk-linux.log)
 *   -r  give the screen back to the console and exit (after a crash)
 *   kernel args: lkl_pci=BUS:DEV.FN for the graphics card, and so on
 *
 * Windows are terminals. Keys go to the window in front, except after
 * Ctrl+] (like tmux's prefix):
 *   n or Enter  new terminal       Tab  next window
 *   arrows      move the window    m    full screen / back
 *   q           close the window   x    quit tuxdesk
 *   p           screenshot (~/tuxdesk-N.ppm)
 *   Ctrl+]      a literal Ctrl+]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "font8x16.h"
#include "kms.h"
#include "term.h"

#define GW 8			/* glyph */
#define GH 16
#define BAR_H 24
#define TITLE_H 20
#define BORDER 1
#define PREFIX 0x1d		/* Ctrl+] */
#define MAX_WIN 16

/* colours */
#define C_DESK		0x2d3236
#define C_BAR		0x1b1d1f
#define C_BAR_TEXT	0xc8ccd0
#define C_BAR_DIM	0x7b8288
#define C_BAR_SEL	0x3b4a57
#define C_TITLE		0x3a3e42
#define C_TITLE_FOCUS	0x44627c
#define C_TITLE_TEXT	0xe6e8ea
#define C_BORDER	0x111314
#define C_CURSOR	0xd8dcdf

struct win {
	int x, y;		/* top left of the frame */
	int cols, rows;
	int saved_x, saved_y, saved_cols, saved_rows, full;
	struct term *t;
};

static struct screen scr;
static uint32_t *back;		/* drawn here, then copied to the screen */
static struct win wins[MAX_WIN];
static int nwin;		/* wins[nwin - 1] is in front */
static int dirty = 1;
static char clock_text[16];

/* drawing */

static void fill(int x, int y, int w, int h, uint32_t c)
{
	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w > scr.width) w = scr.width - x;
	if (y + h > scr.height) h = scr.height - y;
	for (int j = 0; j < h; j++) {
		uint32_t *p = back + (size_t)(y + j) * scr.width + x;
		for (int i = 0; i < w; i++)
			p[i] = c;
	}
}

static void glyph(int x, int y, uint8_t ch, uint32_t fg, uint32_t bg, int opaque)
{
	const unsigned char *g = &font8x16[ch * GH];

	for (int r = 0; r < GH; r++) {
		int py = y + r;
		if (py < 0 || py >= scr.height)
			continue;
		uint32_t *p = back + (size_t)py * scr.width;
		for (int c = 0; c < GW; c++) {
			int px = x + c;
			if (px < 0 || px >= scr.width)
				continue;
			if (g[r] & (0x80 >> c))
				p[px] = fg;
			else if (opaque)
				p[px] = bg;
		}
	}
}

/* text without a background; returns the x after it */
static int text(int x, int y, const char *s, uint32_t fg, int maxw)
{
	for (; *s && maxw >= GW; s++, x += GW, maxw -= GW)
		glyph(x, y, (uint8_t)*s, fg, 0, 0);
	return x;
}

static int frame_w(struct win *w) { return w->cols * GW + 2 * BORDER; }
static int frame_h(struct win *w) { return w->rows * GH + TITLE_H + 2 * BORDER; }

static void draw_win(struct win *w, int focus)
{
	struct term *t = w->t;
	int fw = frame_w(w), fh = frame_h(w);

	fill(w->x, w->y, fw, fh, C_BORDER);
	fill(w->x + BORDER, w->y + BORDER, fw - 2 * BORDER, TITLE_H,
	     focus ? C_TITLE_FOCUS : C_TITLE);
	text(w->x + 8, w->y + BORDER + (TITLE_H - GH) / 2, t->title, C_TITLE_TEXT, fw - 16);

	int ox = w->x + BORDER, oy = w->y + BORDER + TITLE_H;
	for (int r = 0; r < t->rows; r++)
		for (int c = 0; c < t->cols; c++) {
			struct cell *cl = &t->cells[r * t->cols + c];
			int fg = cl->fg, bg = cl->bg;
			if ((cl->attr & ATTR_BOLD) && fg < 8)
				fg += 8;
			if (cl->attr & ATTR_REVERSE) {
				int s = fg; fg = bg; bg = s;
			}
			uint32_t f = term_palette[fg], b = term_palette[bg];
			if (focus && t->cursor_on && r == t->cy && c == t->cx) {
				f = term_palette[0];
				b = C_CURSOR;
			}
			glyph(ox + c * GW, oy + r * GH, cl->ch, f, b, 1);
		}
}

static void draw_bar(void)
{
	fill(0, 0, scr.width, BAR_H, C_BAR);
	int y = (BAR_H - GH) / 2;
	int x = text(10, y, "Hurd64", C_BAR_TEXT, 200) + 20;

	int right = scr.width - 10 - (int)strlen(clock_text) * GW;
	text(right, y, clock_text, C_BAR_TEXT, 200);
	for (int i = 0; i < nwin; i++) {
		char label[40];
		snprintf(label, sizeof label, "%s", wins[i].t->title);
		int w = ((int)strlen(label) + 2) * GW;
		if (x + w > right - 20)
			break;
		if (i == nwin - 1)
			fill(x, 2, w, BAR_H - 4, C_BAR_SEL);
		text(x + GW, y, label, i == nwin - 1 ? C_BAR_TEXT : C_BAR_DIM, w);
		x += w + 4;
	}
}

/* one row of the back buffer (XRGB) to the screen, in the screen's format */
static void put_row(int y)
{
	const uint32_t *src = back + (size_t)y * scr.width;
	uint8_t *dst = (uint8_t *)scr.pixels + (size_t)y * scr.bytes_pitch;

	if (scr.bpp == 32 && scr.rpos == 16 && scr.gpos == 8 && scr.bpos == 0) {
		memcpy(dst, src, scr.width * 4);
		return;
	}
	for (int x = 0; x < scr.width; x++) {
		uint32_t c = src[x];
		uint32_t v = ((c >> 16 & 0xff) >> (8 - scr.rlen)) << scr.rpos
			| ((c >> 8 & 0xff) >> (8 - scr.glen)) << scr.gpos
			| ((c & 0xff) >> (8 - scr.blen)) << scr.bpos;
		if (scr.bpp == 32) {
			((uint32_t *)dst)[x] = v;
		} else {
			dst[3 * x] = v;
			dst[3 * x + 1] = v >> 8;
			dst[3 * x + 2] = v >> 16;
		}
	}
}

static void redraw(void)
{
	fill(0, 0, scr.width, scr.height, C_DESK);
	if (nwin == 0) {
		const char *l1 = "No windows open.";
		const char *l2 = "Press Ctrl+] and then n for a terminal.";
		int cy = scr.height / 2;
		text((scr.width - (int)strlen(l1) * GW) / 2, cy - GH, l1, C_BAR_TEXT, scr.width);
		text((scr.width - (int)strlen(l2) * GW) / 2, cy + 4, l2, C_BAR_DIM, scr.width);
	}
	for (int i = 0; i < nwin; i++)
		draw_win(&wins[i], i == nwin - 1);
	draw_bar();
	for (int y = 0; y < scr.height; y++)
		put_row(y);
	dirty = 0;
}

/* windows */

static void keep_on_screen(struct win *w)
{
	int fw = frame_w(w);
	if (w->x > scr.width - 60) w->x = scr.width - 60;
	if (w->x < 60 - fw) w->x = 60 - fw;
	if (w->y > scr.height - TITLE_H) w->y = scr.height - TITLE_H;
	if (w->y < BAR_H) w->y = BAR_H;
}

static void new_window(void)
{
	if (nwin == MAX_WIN)
		return;
	int cols = 80, rows = 24;
	int maxc = (scr.width - 2 * BORDER) / GW, maxr = (scr.height - BAR_H - TITLE_H - 2 * BORDER) / GH;
	if (cols > maxc) cols = maxc;
	if (rows > maxr) rows = maxr;
	struct term *t = term_new(cols, rows, NULL);
	if (!t)
		return;
	struct win *w = &wins[nwin++];
	memset(w, 0, sizeof *w);
	w->t = t;
	w->cols = cols;
	w->rows = rows;
	/* cascade from the top left */
	w->x = 40 + (nwin - 1) % 8 * 32;
	w->y = BAR_H + 30 + (nwin - 1) % 8 * 28;
	keep_on_screen(w);
	dirty = 1;
}

static void close_window(int i)
{
	term_free(wins[i].t);
	memmove(&wins[i], &wins[i + 1], (nwin - i - 1) * sizeof wins[0]);
	nwin--;
	dirty = 1;
}

static void raise_window(int i)
{
	struct win w = wins[i];
	memmove(&wins[i], &wins[i + 1], (nwin - i - 1) * sizeof wins[0]);
	wins[nwin - 1] = w;
	dirty = 1;
}

static void toggle_full(struct win *w)
{
	if (!w->full) {
		w->saved_x = w->x, w->saved_y = w->y;
		w->saved_cols = w->cols, w->saved_rows = w->rows;
		w->x = 0, w->y = BAR_H;
		w->cols = (scr.width - 2 * BORDER) / GW;
		w->rows = (scr.height - BAR_H - TITLE_H - 2 * BORDER) / GH;
	} else {
		w->x = w->saved_x, w->y = w->saved_y;
		w->cols = w->saved_cols, w->rows = w->saved_rows;
	}
	w->full = !w->full;
	term_resize(w->t, w->cols, w->rows);
	dirty = 1;
}

/* the screen as a PPM file in $HOME */
static void screenshot(void)
{
	static int n;
	char path[512];
	const char *home = getenv("HOME");

	snprintf(path, sizeof path, "%s/tuxdesk-%d.ppm", home ? home : ".", ++n);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", scr.width, scr.height);
	for (size_t i = 0; i < (size_t)scr.width * scr.height; i++) {
		uint32_t c = back[i];
		putc(c >> 16, f);
		putc(c >> 8, f);
		putc(c, f);
	}
	fclose(f);
}

/* keys */

static int quit;

static void command(int key)
{
	struct win *w = nwin ? &wins[nwin - 1] : NULL;

	switch (key) {
	case 'n': case '\r': case '\n': new_window(); break;
	case '\t': if (nwin > 1) raise_window(0); break;
	case 'q': if (w) close_window(nwin - 1); break;
	case 'm': if (w) toggle_full(w); break;
	case 'x': quit = 1; break;
	case 'p': redraw(); screenshot(); break;
	case 'A': if (w) w->y -= 40; break;
	case 'B': if (w) w->y += 40; break;
	case 'C': if (w) w->x += 40; break;
	case 'D': if (w) w->x -= 40; break;
	case PREFIX: if (w) term_write(w->t, "\x1d", 1); break;
	}
	if (w && key >= 'A' && key <= 'D') {
		w->full = 0;
		keep_on_screen(w);
	}
	dirty = 1;
}

static void keys(const char *buf, ssize_t n)
{
	/* 0: typing, 1: after the prefix, 2/3: an arrow's ESC [ after it */
	static int state;
	struct win *w;

	for (ssize_t i = 0; i < n; i++) {
		char c = buf[i];

		switch (state) {
		case 0:
			if (c == PREFIX) {
				state = 1;
				continue;
			}
			/* pass the run of ordinary keys on in one write */
			ssize_t j = i;
			while (j < n && buf[j] != PREFIX)
				j++;
			if (nwin) {
				w = &wins[nwin - 1];
				term_write(w->t, buf + i, j - i);
			}
			i = j - 1;
			continue;
		case 1:
			if (c == 0x1b) {
				state = 2;
				continue;
			}
			state = 0;
			command(c);
			continue;
		case 2:
			state = c == '[' || c == 'O' ? 3 : 0;
			continue;
		case 3:
			state = 0;
			command(c);
			continue;
		}
	}
}

static void tick(void)
{
	char now[16];
	time_t t = time(NULL);

	strftime(now, sizeof now, "%H:%M", localtime(&t));
	if (strcmp(now, clock_text)) {
		strcpy(clock_text, now);
		dirty = 1;
	}
}

static struct termios saved_tio;
static int kbd = 0, raw;

/* on the way out, also when killed or crashing: the keyboard tty back to
   normal, the screen back to the console */
static void cleanup(void)
{
	if (raw)
		tcsetattr(kbd, TCSANOW, &saved_tio);
	raw = 0;
	fb_close();
}

static void fatal_signal(int sig)
{
	cleanup();
	signal(sig, SIG_DFL);
	raise(sig);
}

int main(int argc, char **argv)
{
	const char *driver = NULL, *kbd_path = NULL, *log = "/var/log/tuxdesk-linux.log";
	int mib = 512, opt;
	char args[512] = "";

	while ((opt = getopt(argc, argv, "d:m:k:l:r")) != -1) {
		switch (opt) {
		case 'd': driver = optarg; break;
		case 'm': mib = atoi(optarg); break;
		case 'k': kbd_path = optarg; break;
		case 'l': log = optarg; break;
		case 'r': return fb_console_reset() ? 1 : 0;
		default:
			fprintf(stderr, "usage: tuxdesk [-r] [-d driver] [-m MiB] [-k keyboard] [-l log] [kernel args...]\n");
			return 2;
		}
	}
	for (int i = optind; i < argc; i++) {
		strncat(args, argv[i], sizeof args - strlen(args) - 2);
		strcat(args, " ");
	}
	if (kbd_path && (kbd = open(kbd_path, O_RDWR | O_NOCTTY)) < 0) {
		perror(kbd_path);
		return 1;
	}
	/* the firmware's framebuffer unless a graphics card was named */
	if (!driver && !*args ? fb_open(&scr) : kms_open(&scr, driver, mib, args, log))
		return 1;
	back = malloc((size_t)scr.width * scr.height * 4);
	if (!back)
		return 1;

	/* keys arrive one by one and are not echoed */
	if (isatty(kbd) && tcgetattr(kbd, &saved_tio) == 0) {
		struct termios t = saved_tio;
		cfmakeraw(&t);
		tcsetattr(kbd, TCSANOW, &t);
		raw = 1;
	}
	atexit(cleanup);
	int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGABRT, SIGFPE };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		signal(sigs[i], fatal_signal);
	signal(SIGPIPE, SIG_IGN);

	tick();
	new_window();
	while (!quit) {
		struct pollfd p[MAX_WIN + 1];
		int n = 0;

		p[n++] = (struct pollfd){ .fd = kbd, .events = POLLIN };
		for (int i = 0; i < nwin; i++)
			p[n++] = (struct pollfd){ .fd = wins[i].t->master, .events = POLLIN };
		if (dirty)
			redraw();
		if (poll(p, n, 1000) < 0 && errno != EINTR)
			break;
		tick();

		if (p[0].revents & POLLIN) {
			char buf[256];
			ssize_t r = read(kbd, buf, sizeof buf);
			if (r > 0)
				keys(buf, r);
			else if (r == 0 && !isatty(kbd))
				kbd = -1;	/* a pipe that closed: carry on without keys */
		}
		/* back to front, so a closed window does not shift the ones still to read */
		for (int i = nwin - 1; i >= 0; i--) {
			if (!(p[i + 1].revents & (POLLIN | POLLHUP | POLLERR)))
				continue;
			if (term_read(wins[i].t) < 0)
				close_window(i);
			else if (wins[i].t->dirty) {
				wins[i].t->dirty = 0;
				dirty = 1;
			}
		}
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		/* the redraw is at most one per wakeup; let a burst of output gather */
		if (dirty)
			usleep(8000);
	}
	while (nwin)
		close_window(nwin - 1);
	cleanup();
	kms_close();
	return 0;
}
