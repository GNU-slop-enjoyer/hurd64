/* A terminal: a shell on a pseudo terminal and the grid it draws in.
 * Understands what TERM=linux programs send (colours, cursor moves,
 * erasing, scroll regions). */
#ifndef TERM_H
#define TERM_H
#include <stdint.h>
#include <sys/types.h>

struct cell {
	uint8_t ch;		/* code page 437, like the font */
	uint8_t fg, bg;		/* palette index */
	uint8_t attr;
};

struct term {
	int cols, rows;
	struct cell *cells;
	int cx, cy;		/* cursor */
	int saved_cx, saved_cy;
	int top, bottom;	/* scroll region, inclusive */
	uint8_t fg, bg, attr;
	int cursor_on, wrap_next;
	/* escape parser */
	int state, nparam, param[16], priv;
	uint32_t utf8;
	int utf8_left;
	/* the shell */
	int master;
	pid_t pid;
	char title[64];
	int dirty;
};

#define ATTR_BOLD	1
#define ATTR_REVERSE	2

/* start `shell` (NULL: $SHELL or /bin/sh) on a new pty, cols x rows */
struct term *term_new(int cols, int rows, const char *shell);
void term_free(struct term *t);
/* bytes from the shell; returns 0, or -1 when the shell is gone */
int term_read(struct term *t);
/* bytes typed into the window */
void term_write(struct term *t, const char *buf, size_t n);
void term_resize(struct term *t, int cols, int rows);

extern const uint32_t term_palette[16];

#endif
