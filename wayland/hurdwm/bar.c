/* hurdwm's bar; see bar.h. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>

#include "bar.h"
#include "font8x16.h"

#define C_BAR		0xff1b1d1f
#define C_TEXT		0xffc8ccd0
#define C_DIM		0xff7b8288
#define C_SEL		0xff3b4a57

int bar_scale = 1;

/* a wlr_buffer over pixels in memory */
struct pixbuf {
	struct wlr_buffer base;
	uint32_t *px;
	int stride;
};

static void pb_destroy(struct wlr_buffer *wb)
{
	struct pixbuf *b = wl_container_of(wb, b, base);

	free(b->px);
	free(b);
}

static bool pb_begin(struct wlr_buffer *wb, uint32_t flags, void **data,
		     uint32_t *format, size_t *stride)
{
	struct pixbuf *b = wl_container_of(wb, b, base);

	*data = b->px;
	*format = DRM_FORMAT_ARGB8888;
	*stride = b->stride;
	return true;
}

static void pb_end(struct wlr_buffer *wb)
{
}

static const struct wlr_buffer_impl pb_impl = {
	.destroy = pb_destroy,
	.begin_data_ptr_access = pb_begin,
	.end_data_ptr_access = pb_end,
};

static void fill(uint32_t *px, int w, int x, int y, int fw, int fh, uint32_t c)
{
	for (int j = y; j < y + fh && j < BAR_HEIGHT; j++)
		for (int i = x; i < x + fw && i < w; i++)
			if (i >= 0)
				px[j * w + i] = c;
}

/* text in the console font, bar_scale times as big; returns the x after it */
static int text(uint32_t *px, int w, int x, int y, const char *s, uint32_t c, int maxw)
{
	int z = bar_scale, cw = 8 * z;

	for (; *s && maxw >= cw; s++, x += cw, maxw -= cw) {
		const unsigned char *g = &font8x16[(unsigned char)*s * 16];
		for (int r = 0; r < 16 * z; r++)
			for (int k = 0; k < cw; k++)
				if ((g[r / z] & (0x80 >> (k / z))) && x + k < w)
					px[(y + r) * w + x + k] = c;
	}
	return x;
}

static void now(char out[16])
{
	time_t t = time(NULL);

	strftime(out, 16, "%H:%M", localtime(&t));
}

void bar_init(struct bar *b, struct wlr_scene_tree *parent, int width)
{
	b->width = width;
	bar_scale = width >= 2560 ? 2 : 1;
	b->node = wlr_scene_buffer_create(parent, NULL);
	now(b->clock);
}

int bar_clock_changed(struct bar *b)
{
	char c[16];

	now(c);
	return strcmp(c, b->clock) != 0;
}

void bar_draw(struct bar *b, const char **titles, int n, int focused, const char *hint,
	      const char *prompt)
{
	struct pixbuf *pb = calloc(1, sizeof *pb);
	int w = b->width, z = bar_scale, y = (BAR_HEIGHT - 16 * z) / 2;

	pb->stride = w * 4;
	pb->px = malloc((size_t)pb->stride * BAR_HEIGHT);
	wlr_buffer_init(&pb->base, &pb_impl, w, BAR_HEIGHT);
	fill(pb->px, w, 0, 0, w, BAR_HEIGHT, C_BAR);
	int x = text(pb->px, w, 10 * z, y, "Hurd64", C_TEXT, 200 * z) + 20 * z;
	/* workspaces with windows, and the one shown */
	for (int n = 1; n <= 9; n++) {
		if (n != b->workspace && !(b->used & (1u << n)))
			continue;
		char d[2] = { (char)('0' + n), 0 };
		if (n == b->workspace)
			fill(pb->px, w, x, 2 * z, 24 * z, BAR_HEIGHT - 4 * z, C_SEL);
		text(pb->px, w, x + 8 * z, y, d, n == b->workspace ? C_TEXT : C_DIM, 8 * z);
		x += 24 * z + 2 * z;
	}
	x += 14 * z;

	now(b->clock);
	int right = w - 10 * z - (int)strlen(b->clock) * 8 * z;
	text(pb->px, w, right, y, b->clock, C_TEXT, 100 * z);
	if (prompt) {
		fill(pb->px, w, x, 2 * z, right - 20 * z - x, BAR_HEIGHT - 4 * z, C_SEL);
		int e = text(pb->px, w, x + 8 * z, y, "Run: ", C_DIM, right - 28 * z - x);
		e = text(pb->px, w, e, y, prompt, C_TEXT, right - 28 * z - e);
		fill(pb->px, w, e, y, 8 * z, 16 * z, C_TEXT);	/* the cursor */
		n = 0;
		hint = NULL;
	}
	for (int i = 0; i < n; i++) {
		char label[40];
		strncpy(label, titles[i], sizeof label - 1);
		label[sizeof label - 1] = 0;
		int lw = ((int)strlen(label) + 2) * 8 * z;
		if (x + lw > right - 20 * z)
			break;
		if (i == focused)
			fill(pb->px, w, x, 2 * z, lw, BAR_HEIGHT - 4 * z, C_SEL);
		text(pb->px, w, x + 8 * z, y, label, i == focused ? C_TEXT : C_DIM, lw);
		x += lw + 4 * z;
	}
	if (!n && hint)
		text(pb->px, w, x, y, hint, C_DIM, right - 20 * z - x);
	wlr_scene_buffer_set_buffer(b->node, &pb->base);
	wlr_buffer_drop(&pb->base);
}

/* X = outline, . = fill */
static const char *arrow[] = {
	"X",
	"XX",
	"X.X",
	"X..X",
	"X...X",
	"X....X",
	"X.....X",
	"X......X",
	"X.......X",
	"X........X",
	"X.....XXXXX",
	"X..X..X",
	"X.X X..X",
	"XX  X..X",
	"X    X..X",
	"     X..X",
	"      XX",
};

struct wlr_scene_buffer *bar_arrow(struct wlr_scene_tree *parent)
{
	int w = 11, h = sizeof arrow / sizeof arrow[0];
	struct pixbuf *pb = calloc(1, sizeof *pb);

	pb->stride = w * 4;
	pb->px = calloc((size_t)w * h, 4);
	wlr_buffer_init(&pb->base, &pb_impl, w, h);
	for (int y = 0; y < h; y++)
		for (int x = 0; arrow[y][x]; x++)
			if (arrow[y][x] == 'X')
				pb->px[y * w + x] = 0xff000000;
			else if (arrow[y][x] == '.')
				pb->px[y * w + x] = 0xffffffff;
	struct wlr_scene_buffer *node = wlr_scene_buffer_create(parent, &pb->base);
	wlr_buffer_drop(&pb->base);
	return node;
}
