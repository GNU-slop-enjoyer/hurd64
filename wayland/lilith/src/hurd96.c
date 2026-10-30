/* See hurd96.h. While lilith runs the console stays off the screen; closing
 * the screen (also on fatal signals) hands it back. */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <hurd.h>
#include <mach.h>
#include <mach_error.h>
#include <device/device.h>

#include <linux/input-event-codes.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>

#include "../../../utils/hurd96kbd-events.h"
#include "hurd96.h"

#define FB_GET_INFO	(('f' << 16) | 1)
#define FB_INFO_COUNT	10
#define FB_CONSOLE	(('f' << 16) | 2)

static struct {
	int width, height, pitch, bpp;
	int rpos, rlen, gpos, glen, bpos, blen;
	uint8_t *pixels;
} scr;
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

int hurd96_screen_open(int *width, int *height)
{
	mach_port_t host, master, pager;
	int info[FB_INFO_COUNT];
	mach_msg_type_number_t n = FB_INFO_COUNT;
	kern_return_t kr;

	if ((kr = get_privileged_ports(&host, &master))) {
		fprintf(stderr, "lilith: the screen needs root (%s)\n", mach_error_string(kr));
		return -1;
	}
	if ((kr = device_open(master, D_READ | D_WRITE, "fb", &fbdev))) {
		fprintf(stderr, "lilith: no firmware framebuffer (%s)\n", mach_error_string(kr));
		return -1;
	}
	if ((kr = device_get_status(fbdev, FB_GET_INFO, info, &n)) || n < FB_INFO_COUNT) {
		fprintf(stderr, "lilith: framebuffer info: %s\n", mach_error_string(kr));
		return -1;
	}
	scr.width = info[0], scr.height = info[1], scr.pitch = info[2], scr.bpp = info[3];
	scr.rpos = info[4], scr.rlen = info[5];
	scr.gpos = info[6], scr.glen = info[7];
	scr.bpos = info[8], scr.blen = info[9];
	if (scr.bpp != 32 && scr.bpp != 24) {
		fprintf(stderr, "lilith: %d bits per pixel is not supported\n", scr.bpp);
		return -1;
	}
	vm_size_t size = ((vm_size_t)scr.pitch * scr.height + vm_page_size - 1) & ~(vm_page_size - 1);
	vm_address_t addr = 0;
	if ((kr = device_map(fbdev, VM_PROT_READ | VM_PROT_WRITE, 0, size, &pager, 0))
	    || (kr = vm_map(mach_task_self(), &addr, size, 0, 1, pager, 0, 0,
			    VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
			    VM_INHERIT_NONE))) {
		fprintf(stderr, "lilith: cannot map the framebuffer: %s\n", mach_error_string(kr));
		return -1;
	}
	scr.pixels = (uint8_t *)addr;
	console(0);
	int sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGHUP, SIGQUIT };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		signal(sigs[i], fatal);
	*width = scr.width;
	*height = scr.height;
	return 0;
}

static void put_rect(const uint8_t *src, size_t stride, int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > scr.width) x1 = scr.width;
	if (y1 > scr.height) y1 = scr.height;
	if (x0 >= x1 || y0 >= y1)
		return;
	int plain = scr.bpp == 32 && scr.rpos == 16 && scr.gpos == 8 && scr.bpos == 0;
	for (int y = y0; y < y1; y++) {
		const uint32_t *in = (const uint32_t *)(src + (size_t)y * stride) + x0;
		uint8_t *out = scr.pixels + (size_t)y * scr.pitch;
		if (plain) {
			memcpy(out + x0 * 4, in, (x1 - x0) * 4);
			continue;
		}
		for (int x = x0; x < x1; x++) {
			uint32_t c = in[x - x0];
			uint32_t v = ((c >> 16 & 0xff) >> (8 - scr.rlen)) << scr.rpos
				| ((c >> 8 & 0xff) >> (8 - scr.glen)) << scr.gpos
				| ((c & 0xff) >> (8 - scr.blen)) << scr.bpos;
			if (scr.bpp == 32) {
				((uint32_t *)out)[x] = v;
			} else {
				out[3 * x] = v;
				out[3 * x + 1] = v >> 8;
				out[3 * x + 2] = v >> 16;
			}
		}
	}
}

void hurd96_screen_put(const void *src, size_t stride, const pixman_region32_t *damage)
{
	if (!damage) {
		put_rect(src, stride, 0, 0, scr.width, scr.height);
		return;
	}
	int n;
	const pixman_box32_t *r = pixman_region32_rectangles((pixman_region32_t *)damage, &n);
	for (int i = 0; i < n; i++)
		put_rect(src, stride, r[i].x1, r[i].y1, r[i].x2, r[i].y2);
}

void hurd96_screen_close(void)
{
	console(1);
	if (fbdev != MACH_PORT_NULL)
		device_close(fbdev);
	fbdev = MACH_PORT_NULL;
}

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

static struct wlr_keyboard keyboard;
static struct wlr_pointer pointer;
static struct wl_event_source *source;
static int sock = -1;

static const struct wlr_keyboard_impl keyboard_impl = { .name = "hurd96-keyboard" };
static const struct wlr_pointer_impl pointer_impl = { .name = "hurd96-pointer" };

static uint32_t now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void key(uint32_t code, int pressed)
{
	struct wlr_keyboard_key_event ev = {
		.time_msec = now_ms(), .keycode = code, .update_state = true,
		.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
	};

	wlr_keyboard_notify_key(&keyboard, &ev);
}

static void motion(int dx, int dy)
{
	struct wlr_pointer_motion_event ev = {
		.pointer = &pointer, .time_msec = now_ms(),
		.delta_x = dx, .delta_y = dy, .unaccel_dx = dx, .unaccel_dy = dy,
	};

	wl_signal_emit_mutable(&pointer.events.motion, &ev);
	wl_signal_emit_mutable(&pointer.events.frame, &pointer);
}

static void button(int b, int pressed)
{
	static const uint32_t codes[] = { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT };
	struct wlr_pointer_button_event ev = {
		.pointer = &pointer, .time_msec = now_ms(),
		.state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
	};

	if (b < 0 || b > 2)
		return;
	ev.button = codes[b];
	wl_signal_emit_mutable(&pointer.events.button, &ev);
	wl_signal_emit_mutable(&pointer.events.frame, &pointer);
}

static void wheel(int steps)
{
	/* away from the user scrolls up: a negative vertical axis value */
	struct wlr_pointer_axis_event ev = {
		.pointer = &pointer, .time_msec = now_ms(),
		.source = WL_POINTER_AXIS_SOURCE_WHEEL,
		.orientation = WL_POINTER_AXIS_VERTICAL_SCROLL,
		.relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL,
		.delta = -15.0 * steps, .delta_discrete = -120 * steps,
	};

	wl_signal_emit_mutable(&pointer.events.axis, &ev);
	wl_signal_emit_mutable(&pointer.events.frame, &pointer);
}

static int readable(int fd, uint32_t mask, void *data)
{
	struct hurd96kbd_event ev[32];
	ssize_t r = read(fd, ev, sizeof ev);
	int dx = 0, dy = 0;

	if (r <= 0) {
		fprintf(stderr, "lilith: the keyboard went away\n");
		wl_event_source_remove(source);
		source = NULL;
		return 0;
	}
	for (size_t i = 0; i < (size_t)r / sizeof ev[0]; i++) {
		switch (ev[i].type) {
		case HURD96KBD_KEY:
			if (ev[i].code < 256 && hid_to_linux[ev[i].code])
				key(hid_to_linux[ev[i].code], ev[i].value);
			break;
		case HURD96KBD_MOTION:
			/* gather x and y of one report into one move */
			if (ev[i].code == 0)
				dx += ev[i].value;
			else if (ev[i].code == 1)
				dy += ev[i].value;
			else
				wheel(ev[i].value);
			break;
		case HURD96KBD_BUTTON:
			if (dx || dy)
				motion(dx, dy);
			dx = dy = 0;
			button(ev[i].code, ev[i].value);
			break;
		}
	}
	if (dx || dy)
		motion(dx, dy);
	return 0;
}

int hurd96_input_start(struct wl_event_loop *loop, struct wlr_backend *backend)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	strncpy(a.sun_path, HURD96KBD_SOCKET, sizeof a.sun_path - 1);
	if (sock < 0 || connect(sock, (struct sockaddr *)&a, sizeof a) < 0) {
		fprintf(stderr, "lilith: no keyboard (is hurd96kbd running?)\n");
		return -1;
	}
	wlr_keyboard_init(&keyboard, &keyboard_impl, "hurd96-keyboard");
	wlr_pointer_init(&pointer, &pointer_impl, "hurd96-pointer");
	wl_signal_emit_mutable(&backend->events.new_input, &keyboard.base);
	wl_signal_emit_mutable(&backend->events.new_input, &pointer.base);
	source = wl_event_loop_add_fd(loop, sock, WL_EVENT_READABLE, readable, NULL);
	return 0;
}

void hurd96_input_stop(void)
{
	if (source)
		wl_event_source_remove(source);
	source = NULL;
	if (sock >= 0)
		close(sock);
	sock = -1;
}
