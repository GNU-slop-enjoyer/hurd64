/* The screen, through Linux's DRM/KMS running inside this process (LKL). */
#ifndef KMS_H
#define KMS_H
#include <stdint.h>

struct screen {
	int width, height;
	int pitch;		/* in pixels */
	uint32_t *pixels;	/* the scanout buffer (XRGB8888), mapped here */
};

/* Boot Linux with the given kernel arguments (lkl_pci=... for the card),
   wait for a card driven by `driver` (NULL: any card that is not vkms),
   set its preferred mode on the first connected output. Linux's own
   messages go to `log`. Returns 0 or -1 with a message on stderr. */
int kms_open(struct screen *s, const char *driver, int ram_mib,
	     const char *kernel_args, const char *log);

void kms_close(void);

#endif
