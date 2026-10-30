/* The screen: through Linux's DRM/KMS running inside this process (LKL),
   or the framebuffer the firmware set up. */
#ifndef KMS_H
#define KMS_H
#include <stdint.h>

struct screen {
	int width, height;
	int pitch;		/* in 32-bit pixels */
	int bytes_pitch, bpp;
	int rpos, rlen, gpos, glen, bpos, blen;	/* where the colours are */
	uint32_t *pixels;	/* the scanout buffer, mapped here */
};

/* the firmware's framebuffer (GNU Mach "fb" device), no graphics driver */
int fb_open(struct screen *s);
void fb_close(void);
/* the screen back to the console (after a tuxdesk that was killed) */
int fb_console_reset(void);

/* Boot Linux with the given kernel arguments (lkl_pci=... for the card),
   wait for a card driven by `driver` (NULL: any card that is not vkms),
   set its preferred mode on the first connected output. Linux's own
   messages go to `log`. Returns 0 or -1 with a message on stderr. */
int kms_open(struct screen *s, const char *driver, int ram_mib,
	     const char *kernel_args, const char *log);

void kms_close(void);

#endif
