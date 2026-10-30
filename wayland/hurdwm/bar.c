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

static int text(uint32_t *px, int w, int x, int y, const char *s, uint32_t c, int maxw)
{
	for (; *s && maxw >= 8; s++, x += 8, maxw -= 8) {
		const unsigned char *g = &font8x16[(unsigned char)*s * 16];
		for (int r = 0; r < 16; r++)
			for (int k = 0; k < 8; k++)
				if ((g[r] & (0x80 >> k)) && x + k < w)
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
	b->node = wlr_scene_buffer_create(parent, NULL);
	now(b->clock);
}

int bar_clock_changed(struct bar *b)
{
	char c[16];

	now(c);
	return strcmp(c, b->clock) != 0;
}

void bar_draw(struct bar *b, const char **titles, int n, int focused, const char *hint)
{
	struct pixbuf *pb = calloc(1, sizeof *pb);
	int w = b->width, y = (BAR_HEIGHT - 16) / 2;

	pb->stride = w * 4;
	pb->px = malloc((size_t)pb->stride * BAR_HEIGHT);
	wlr_buffer_init(&pb->base, &pb_impl, w, BAR_HEIGHT);
	fill(pb->px, w, 0, 0, w, BAR_HEIGHT, C_BAR);
	int x = text(pb->px, w, 10, y, "Hurd64", C_TEXT, 200) + 20;

	now(b->clock);
	int right = w - 10 - (int)strlen(b->clock) * 8;
	text(pb->px, w, right, y, b->clock, C_TEXT, 100);
	for (int i = 0; i < n; i++) {
		char label[40];
		strncpy(label, titles[i], sizeof label - 1);
		label[sizeof label - 1] = 0;
		int lw = ((int)strlen(label) + 2) * 8;
		if (x + lw > right - 20)
			break;
		if (i == focused)
			fill(pb->px, w, x, 2, lw, BAR_HEIGHT - 4, C_SEL);
		text(pb->px, w, x + 8, y, label, i == focused ? C_TEXT : C_DIM, lw);
		x += lw + 4;
	}
	if (!n && hint)
		text(pb->px, w, x, y, hint, C_DIM, right - 20 - x);
	wlr_scene_buffer_set_buffer(b->node, &pb->base);
	wlr_buffer_drop(&pb->base);
}
