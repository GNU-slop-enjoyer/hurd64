/* lilith on hurd96: the screen is the firmware framebuffer (GNU Mach's "fb"
 * device) and input comes from hurd96kbd, so lilith runs on wlroots' headless
 * backend with the pixman renderer. */
#ifndef HURD96_H
#define HURD96_H
#include <stddef.h>
#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>

int hurd96_screen_open(int *width, int *height);
/* XRGB8888 rows to the screen; only the damaged rectangles when damage is given */
void hurd96_screen_put(const void *src, size_t stride, const pixman_region32_t *damage);
void hurd96_screen_close(void);
/* a keyboard and a pointer on the backend's new_input signal */
int hurd96_input_start(struct wl_event_loop *loop, struct wlr_backend *backend);
void hurd96_input_stop(void);

#endif
