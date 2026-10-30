/* hurdwm's screen: GNU Mach's "fb" device (patches/gnumach/fbdev.patch).
 * While hurdwm runs the console stays off the screen; screen_close (also
 * called on fatal signals) hands it back. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hurd.h>
#include <mach.h>
#include <mach_error.h>
#include <device/device.h>

#include "screen.h"

#define FB_GET_INFO	(('f' << 16) | 1)
#define FB_INFO_COUNT	10
#define FB_CONSOLE	(('f' << 16) | 2)

static mach_port_t fbdev = MACH_PORT_NULL;

static void console(int on)
{
	int v = on;

	if (fbdev != MACH_PORT_NULL)
		device_set_status(fbdev, FB_CONSOLE, &v, 1);
}

static void fatal(int sig)
{
	console(1);
	signal(sig, SIG_DFL);
	raise(sig);
}

int screen_open(struct screen *s)
{
	mach_port_t host, master, pager;
	int info[FB_INFO_COUNT];
	mach_msg_type_number_t n = FB_INFO_COUNT;
	kern_return_t kr;

	if ((kr = get_privileged_ports(&host, &master))) {
		fprintf(stderr, "hurdwm: the screen needs root (%s)\n", mach_error_string(kr));
		return -1;
	}
	if ((kr = device_open(master, D_READ | D_WRITE, "fb", &fbdev))) {
		fprintf(stderr, "hurdwm: no firmware framebuffer (%s)\n", mach_error_string(kr));
		return -1;
	}
	if ((kr = device_get_status(fbdev, FB_GET_INFO, info, &n)) || n < FB_INFO_COUNT) {
		fprintf(stderr, "hurdwm: framebuffer info: %s\n", mach_error_string(kr));
		return -1;
	}
	s->width = info[0], s->height = info[1], s->pitch = info[2], s->bpp = info[3];
	s->rpos = info[4], s->rlen = info[5];
	s->gpos = info[6], s->glen = info[7];
	s->bpos = info[8], s->blen = info[9];
	if (s->bpp != 32 && s->bpp != 24) {
		fprintf(stderr, "hurdwm: %d bits per pixel is not supported\n", s->bpp);
		return -1;
	}
	vm_size_t size = ((vm_size_t)s->pitch * s->height + vm_page_size - 1) & ~(vm_page_size - 1);
	vm_address_t addr = 0;
	if ((kr = device_map(fbdev, VM_PROT_READ | VM_PROT_WRITE, 0, size, &pager, 0))
	    || (kr = vm_map(mach_task_self(), &addr, size, 0, 1, pager, 0, 0,
			    VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
			    VM_INHERIT_NONE))) {
		fprintf(stderr, "hurdwm: cannot map the framebuffer: %s\n", mach_error_string(kr));
		return -1;
	}
	s->pixels = (uint8_t *)addr;
	s->copy = calloc((size_t)s->width * s->height, 4);
	console(0);
	int sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGHUP, SIGQUIT };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		signal(sigs[i], fatal);
	return 0;
}

static void put_rect(struct screen *s, const uint8_t *src, size_t stride,
		     int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > s->width) x1 = s->width;
	if (y1 > s->height) y1 = s->height;
	if (x0 >= x1 || y0 >= y1)
		return;
	int plain = s->bpp == 32 && s->rpos == 16 && s->gpos == 8 && s->bpos == 0;
	for (int y = y0; y < y1; y++) {
		const uint32_t *in = (const uint32_t *)(src + (size_t)y * stride) + x0;
		uint8_t *out = s->pixels + (size_t)y * s->pitch;
		if (s->copy)
			memcpy(s->copy + (size_t)y * s->width + x0, in, (x1 - x0) * 4);
		if (plain) {
			memcpy(out + x0 * 4, in, (x1 - x0) * 4);
			continue;
		}
		for (int x = x0; x < x1; x++) {
			uint32_t c = in[x - x0];
			uint32_t v = ((c >> 16 & 0xff) >> (8 - s->rlen)) << s->rpos
				| ((c >> 8 & 0xff) >> (8 - s->glen)) << s->gpos
				| ((c & 0xff) >> (8 - s->blen)) << s->bpos;
			if (s->bpp == 32) {
				((uint32_t *)out)[x] = v;
			} else {
				out[3 * x] = v;
				out[3 * x + 1] = v >> 8;
				out[3 * x + 2] = v >> 16;
			}
		}
	}
}

void screen_put(struct screen *s, const void *src, size_t stride,
		const pixman_region32_t *damage)
{
	if (!damage) {
		put_rect(s, src, stride, 0, 0, s->width, s->height);
		return;
	}
	int n;
	const pixman_box32_t *r = pixman_region32_rectangles((pixman_region32_t *)damage, &n);
	for (int i = 0; i < n; i++)
		put_rect(s, src, stride, r[i].x1, r[i].y1, r[i].x2, r[i].y2);
}

void screen_save(struct screen *s, const char *ppm)
{
	FILE *f = fopen(ppm, "wb");

	if (!f || !s->copy) {
		if (f)
			fclose(f);
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);
	for (size_t i = 0; i < (size_t)s->width * s->height; i++) {
		uint32_t c = s->copy[i];
		putc(c >> 16, f);
		putc(c >> 8, f);
		putc(c, f);
	}
	fclose(f);
}

void screen_close(struct screen *s)
{
	console(1);
	if (fbdev != MACH_PORT_NULL)
		device_close(fbdev);
	fbdev = MACH_PORT_NULL;
}
