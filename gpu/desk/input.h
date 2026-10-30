/* Keys from tuxkbd's socket (USB keyboard). */
#ifndef INPUT_H
#define INPUT_H
#include <stddef.h>

struct input_out {
	void (*bytes)(const char *buf, size_t n);	/* typed text and escape sequences */
	void (*command)(int cmd);			/* Super+key: a tuxdesk command */
};

/* connect to tuxkbd; -1 if it is not running */
int input_connect(void);
/* read what is there; -1 when tuxkbd hung up */
int input_read(int fd, struct input_out *out);

#endif
