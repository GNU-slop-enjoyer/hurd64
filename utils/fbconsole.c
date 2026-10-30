/* fbconsole: give the screen back to the console after a program that drew
 * on GNU Mach's "fb" device died without doing so. */
#define _GNU_SOURCE
#include <stdio.h>

#include <hurd.h>
#include <mach.h>
#include <device/device.h>

#define FB_CONSOLE	(('f' << 16) | 2)

int main(void)
{
	mach_port_t host, master, fb;
	int on = 1;

	if (get_privileged_ports(&host, &master)
	    || device_open(master, D_READ | D_WRITE, "fb", &fb)
	    || device_set_status(fb, FB_CONSOLE, &on, 1)) {
		fprintf(stderr, "fbconsole: no framebuffer\n");
		return 1;
	}
	return 0;
}
