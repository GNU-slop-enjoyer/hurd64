/* The screen the firmware set up (UEFI GOP), through GNU Mach's "fb"
 * device: no graphics driver needed. While tuxdesk holds the device the
 * kernel console stays off the screen; it comes back when tuxdesk exits. */
#include <stdio.h>

#include "kms.h"

#ifdef __GNU__
#include <hurd.h>
#include <mach.h>
#include <device/device.h>
#include <mach_error.h>

/* from GNU Mach's i386at/kdfb.h */
#define FB_GET_INFO	(('f' << 16) | 1)
#define FB_INFO_COUNT	10
#define FB_CONSOLE	(('f' << 16) | 2)

static mach_port_t fbdev = MACH_PORT_NULL;

static void console(mach_port_t dev, int on)
{
	int v = on;

	device_set_status(dev, FB_CONSOLE, &v, 1);
}

/* Give the screen back to the console. GNU Mach does not do it when
   tuxdesk goes away, so a tuxdesk that was killed leaves the screen as it
   was; `tuxdesk -r` hands it back. */
void fb_close(void)
{
	if (fbdev != MACH_PORT_NULL) {
		console(fbdev, 1);
		device_close(fbdev);
		fbdev = MACH_PORT_NULL;
	}
}

int fb_open(struct screen *s)
{
	mach_port_t host, master, dev, pager;
	int info[FB_INFO_COUNT];
	mach_msg_type_number_t n = FB_INFO_COUNT;
	kern_return_t kr;

	if ((kr = get_privileged_ports(&host, &master))) {
		fprintf(stderr, "tuxdesk: the screen needs root (%s)\n", mach_error_string(kr));
		return -1;
	}
	if ((kr = device_open(master, D_READ | D_WRITE, "fb", &dev))) {
		fprintf(stderr, "tuxdesk: no firmware framebuffer (%s)\n", mach_error_string(kr));
		return -1;
	}
	if ((kr = device_get_status(dev, FB_GET_INFO, info, &n)) || n < FB_INFO_COUNT) {
		fprintf(stderr, "tuxdesk: framebuffer info: %s\n", mach_error_string(kr));
		return -1;
	}
	s->width = info[0];
	s->height = info[1];
	s->bytes_pitch = info[2];
	s->bpp = info[3];
	s->rpos = info[4], s->rlen = info[5];
	s->gpos = info[6], s->glen = info[7];
	s->bpos = info[8], s->blen = info[9];
	if (s->bpp != 32 && s->bpp != 24) {
		fprintf(stderr, "tuxdesk: %d bits per pixel is not supported\n", s->bpp);
		return -1;
	}

	vm_size_t size = (vm_size_t)s->bytes_pitch * s->height;
	vm_address_t addr = 0;
	size = (size + vm_page_size - 1) & ~(vm_page_size - 1);
	if ((kr = device_map(dev, VM_PROT_READ | VM_PROT_WRITE, 0, size, &pager, 0))
	    || (kr = vm_map(mach_task_self(), &addr, size, 0, 1, pager, 0, 0,
			    VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
			    VM_INHERIT_NONE))) {
		fprintf(stderr, "tuxdesk: cannot map the framebuffer: %s\n", mach_error_string(kr));
		return -1;
	}
	s->pixels = (uint32_t *)addr;
	s->pitch = s->bytes_pitch / 4;
	fbdev = dev;
	console(dev, 0);
	fprintf(stderr, "tuxdesk: firmware framebuffer %dx%d, %d bpp\n", s->width, s->height, s->bpp);
	return 0;
}
int fb_console_reset(void)
{
	mach_port_t host, master, dev;

	if (get_privileged_ports(&host, &master)
	    || device_open(master, D_READ | D_WRITE, "fb", &dev))
		return -1;
	console(dev, 1);
	device_close(dev);
	return 0;
}
#else
int fb_open(struct screen *s)
{
	fprintf(stderr, "tuxdesk: the firmware framebuffer is a GNU Mach device\n");
	return -1;
}


void fb_close(void)
{
}

int fb_console_reset(void)
{
	return -1;
}
#endif
