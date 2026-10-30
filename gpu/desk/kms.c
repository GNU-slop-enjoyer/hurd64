/* Screen setup through Linux's KMS inside LKL; see kms.h.
 * The same steps as gpu/test/kms-test.c, which is the place to debug them. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lkl.h>
#include <lkl_host.h>
#include <lkl/linux/mman.h>

#include "../test/linux-drm.h"
#include "kms.h"

static int logfd = -1, started;

static void lkl_print(const char *str, int len)
{
	if (logfd >= 0 && write(logfd, str, len) < 0)
		return;
}

static long ioc(int fd, unsigned long req, void *arg, const char *what)
{
	long r = lkl_sys_ioctl(fd, req, (long)arg);

	if (r < 0)
		fprintf(stderr, "tuxdesk: %s: %s\n", what, lkl_strerror(r));
	return r;
}

/* the first card whose driver is `driver` (NULL: not vkms) */
static int find_card(const char *driver)
{
	for (int t = 0; t < 600; t++) {
		for (int c = 0; c < 4; c++) {
			char path[32], name[32] = "";
			struct drm_version v = { .name_len = sizeof name - 1, .name = name };

			snprintf(path, sizeof path, "/dev/dri/card%d", c);
			int fd = lkl_sys_open(path, LKL_O_RDWR, 0);
			if (fd < 0)
				continue;
			if (lkl_sys_ioctl(fd, DRM_IOCTL_VERSION, (long)&v) == 0
			    && (driver ? !strcmp(name, driver) : strcmp(name, "vkms") != 0)) {
				fprintf(stderr, "tuxdesk: %s (%s)\n", path, name);
				return fd;
			}
			lkl_sys_close(fd);
		}
		usleep(100000);	/* a GPU probes in the background */
	}
	return -1;
}

int kms_open(struct screen *s, const char *driver, int ram_mib,
	     const char *kernel_args, const char *log)
{
	char cmd[1024];

	if (log)
		logfd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	lkl_host_ops.print = lkl_print;
	snprintf(cmd, sizeof cmd, "mem=%dM loglevel=6 %s", ram_mib, kernel_args ? kernel_args : "");
	if (lkl_init(&lkl_host_ops) < 0 || lkl_start_kernel(cmd) < 0) {
		fprintf(stderr, "tuxdesk: Linux did not start\n");
		return -1;
	}
	started = 1;
	lkl_sys_mkdir("/dev", 0755);
	if (lkl_sys_mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0) {
		fprintf(stderr, "tuxdesk: no devtmpfs\n");
		return -1;
	}
	int fd = find_card(driver);
	if (fd < 0) {
		fprintf(stderr, "tuxdesk: no %s graphics card\n", driver ? driver : "");
		return -1;
	}
	ioc(fd, DRM_IOCTL_SET_MASTER, NULL, "SET_MASTER");

	struct drm_mode_card_res res = { 0 };
	uint32_t crtcs[16], conns[16], encs[16];
	if (ioc(fd, DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES") < 0
	    || res.count_crtcs > 16 || res.count_connectors > 16 || res.count_encoders > 16)
		return -1;
	res.count_fbs = 0;
	res.crtc_id_ptr = (uintptr_t)crtcs;
	res.connector_id_ptr = (uintptr_t)conns;
	res.encoder_id_ptr = (uintptr_t)encs;
	if (ioc(fd, DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES") < 0)
		return -1;

	static struct drm_mode_modeinfo modes[64];
	struct drm_mode_get_connector conn;
	uint32_t conn_id = 0;
	for (uint32_t i = 0; i < res.count_connectors && !conn_id; i++) {
		memset(&conn, 0, sizeof conn);
		conn.connector_id = conns[i];
		if (ioc(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn, "GETCONNECTOR") < 0
		    || conn.connection != DRM_MODE_CONNECTED || !conn.count_modes)
			continue;
		if (conn.count_modes > 64)
			conn.count_modes = 64;
		conn.count_props = conn.count_encoders = 0;
		conn.modes_ptr = (uintptr_t)modes;
		if (ioc(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn, "GETCONNECTOR") == 0)
			conn_id = conns[i];
	}
	if (!conn_id) {
		fprintf(stderr, "tuxdesk: no screen connected\n");
		return -1;
	}
	struct drm_mode_modeinfo *m = &modes[0];
	struct drm_mode_get_encoder enc = { .encoder_id = conn.encoder_id };
	uint32_t crtc_id = crtcs[0];
	if (conn.encoder_id && ioc(fd, DRM_IOCTL_MODE_GETENCODER, &enc, "GETENCODER") == 0
	    && enc.crtc_id)
		crtc_id = enc.crtc_id;

	struct drm_mode_create_dumb db = { .width = m->hdisplay, .height = m->vdisplay, .bpp = 32 };
	if (ioc(fd, DRM_IOCTL_MODE_CREATE_DUMB, &db, "CREATE_DUMB") < 0)
		return -1;
	struct drm_mode_fb_cmd fb = { .width = db.width, .height = db.height, .pitch = db.pitch,
				      .bpp = 32, .depth = 24, .handle = db.handle };
	if (ioc(fd, DRM_IOCTL_MODE_ADDFB, &fb, "ADDFB") < 0)
		return -1;
	struct drm_mode_crtc set = { .set_connectors_ptr = (uintptr_t)&conn_id,
				     .count_connectors = 1, .crtc_id = crtc_id,
				     .fb_id = fb.fb_id, .mode_valid = 1, .mode = *m };
	if (ioc(fd, DRM_IOCTL_MODE_SETCRTC, &set, "SETCRTC") < 0)
		return -1;

	/* Map it only now: showing it pins it in VRAM, and moving it there
	   drops earlier mappings, which LKL (no page faults) cannot bring
	   back. MAP_POPULATE maps every page up front for the same reason. */
	struct drm_mode_map_dumb map = { .handle = db.handle };
	if (ioc(fd, DRM_IOCTL_MODE_MAP_DUMB, &map, "MAP_DUMB") < 0)
		return -1;
	long p = (long)lkl_sys_mmap(NULL, db.size, LKL_PROT_READ | LKL_PROT_WRITE,
				    LKL_MAP_SHARED | LKL_MAP_POPULATE, fd, map.offset);
	if (p < 0 && p > -4096) {
		fprintf(stderr, "tuxdesk: mmap: %s\n", lkl_strerror(p));
		return -1;
	}
	s->width = db.width;
	s->height = db.height;
	s->pitch = db.pitch / 4;
	s->bytes_pitch = db.pitch;
	s->bpp = 32;
	s->rpos = 16, s->gpos = 8, s->bpos = 0;
	s->rlen = s->glen = s->blen = 8;
	s->pixels = (uint32_t *)p;
	fprintf(stderr, "tuxdesk: %dx%d at %u Hz\n", s->width, s->height, m->vrefresh);
	return 0;
}

void kms_close(void)
{
	if (started)
		lkl_sys_halt();
	started = 0;
}
