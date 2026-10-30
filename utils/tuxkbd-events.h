/* What tuxkbd sends to a program that takes over the keyboard (tuxdesk):
 * connect to TUXKBD_SOCKET and read these records. While a program is
 * connected the console gets no keys; when it disconnects they go back to
 * the console. */
#ifndef TUXKBD_EVENTS_H
#define TUXKBD_EVENTS_H
#include <stdint.h>

#define TUXKBD_SOCKET	"/run/tuxkbd.sock"

enum {
	TUXKBD_KEY = 1,		/* code: USB HID usage, value: 1 down, 0 up */
	TUXKBD_MOTION,		/* code: 0 x, 1 y, 2 wheel; value: relative */
	TUXKBD_BUTTON,		/* code: 0 left, 1 middle, 2 right; value: 1 down, 0 up */
};

struct tuxkbd_event {
	uint16_t type, code;
	int32_t value;
};

#endif
