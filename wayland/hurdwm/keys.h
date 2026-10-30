/* Keys for hurdwm: USB keyboard events from tuxkbd, or bytes from a tty or
 * fifo (-k) for testing, both turned into Linux key codes. */
#ifndef KEYS_H
#define KEYS_H
#include <stdbool.h>
#include <stdint.h>

typedef void (*key_fn)(void *data, uint32_t linux_code, bool pressed);

/* path NULL: tuxkbd's socket. Returns a descriptor to watch, or -1. */
int keys_open(const char *path);
/* what is there to read; -1 when the source went away */
int keys_read(int fd, key_fn fn, void *data);
void keys_close(void);

#endif
