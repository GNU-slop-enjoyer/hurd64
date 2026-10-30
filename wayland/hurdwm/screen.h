/* The firmware's framebuffer (GNU Mach "fb" device) as hurdwm's screen. */
#ifndef SCREEN_H
#define SCREEN_H
#include <stddef.h>
#include <stdint.h>
#include <pixman.h>

struct screen {
	int width, height, pitch, bpp;
	int rpos, rlen, gpos, glen, bpos, blen;
	uint8_t *pixels;
	uint32_t *copy;		/* what is on the screen, XRGB, for screenshots */
};

int screen_open(struct screen *s);
/* XRGB8888 rows (wlroots' output buffer) to the screen; only the damaged
   rectangles when damage is given */
void screen_put(struct screen *s, const void *src, size_t stride,
		const pixman_region32_t *damage);
void screen_save(struct screen *s, const char *ppm);
void screen_close(struct screen *s);

#endif
