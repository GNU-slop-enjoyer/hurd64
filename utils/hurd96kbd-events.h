/* What hurd96kbd sends to a program that takes over the keyboard (lilith):
 * connect to HURD96KBD_SOCKET and read these records. While a program is
 * connected the console gets no keys; when it disconnects they go back to
 * the console. */
#ifndef HURD96KBD_EVENTS_H
#define HURD96KBD_EVENTS_H
#include <stdint.h>

#define HURD96KBD_SOCKET	"/run/hurd96kbd.sock"

enum {
	HURD96KBD_KEY = 1,		/* code: USB HID usage, value: 1 down, 0 up */
	HURD96KBD_MOTION,		/* code: 0 x, 1 y, 2 wheel; value: relative */
	HURD96KBD_BUTTON,		/* code: 0 left, 1 middle, 2 right; value: 1 down, 0 up */
};

struct hurd96kbd_event {
	uint16_t type, code;
	int32_t value;
};

#endif
