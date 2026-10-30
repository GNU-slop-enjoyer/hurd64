/* Input for hurdwm: USB keyboard and mouse events from tuxkbd, or bytes
 * from a tty or fifo (-k) for testing. Keys become Linux key codes. */
#ifndef KEYS_H
#define KEYS_H
#include <stdbool.h>
#include <stdint.h>

struct input_fns {
	void (*key)(void *data, uint32_t linux_code, bool pressed);
	void (*motion)(void *data, int dx, int dy);
	void (*button)(void *data, int button, bool pressed);	/* 0 left, 1 middle, 2 right */
	void (*wheel)(void *data, int steps);			/* positive: away from the user */
};

/* path NULL: tuxkbd's socket. Returns a descriptor to watch, or -1. */
int keys_open(const char *path);
/* what is there to read; -1 when the source went away */
int keys_read(int fd, const struct input_fns *fns, void *data);
void keys_close(void);

#endif
