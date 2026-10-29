/* kms-test: light up a display through Linux's DRM/KMS inside LKL.
 *
 *   kms-test [driver] [MiB] [kernel arguments...]   driver: vkms (default), amdgpu, bochs-drm
 *
 * Boots LKL (Linux with amdgpu and vkms), opens the card of that driver in it, picks
 * the first connected connector and its preferred mode, makes a dumb
 * buffer, maps it into this process, draws colour bars and shows it. The
 * buffer is mapped twice to check that both mappings see the same pages.
 * vkms is Linux's virtual display; a real card is passed in with lkl_pci=.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lkl.h>
#include <lkl_host.h>
#include <lkl/linux/mman.h>

#include "linux-drm.h"

static long ioc(int fd, unsigned long req, void *arg, const char *what)
{
	long r = lkl_sys_ioctl(fd, req, (long)arg);

	if (r < 0)
		fprintf(stderr, "kms-test: %s: %s\n", what, lkl_strerror(r));
	return r;
}

int main(int argc, char **argv)
{
	const char *driver = argc > 1 ? argv[1] : "vkms";
	int mib = argc > 2 ? atoi(argv[2]) : 256;
	char cmd[1024], path[32];

	setvbuf(stdout, NULL, _IONBF, 0);
	snprintf(cmd, sizeof cmd, "mem=%dM loglevel=4", mib > 0 ? mib : 256);
	for (int i = 3; i < argc; i++) {
		strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1);
		strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 1);
	}
	if (lkl_init(&lkl_host_ops) < 0 || lkl_start_kernel(cmd) < 0) {
		fprintf(stderr, "kms-test: kernel did not start\n");
		return 1;
	}
	lkl_sys_mkdir("/dev", 0755);
	if (lkl_sys_mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0) {
		fprintf(stderr, "kms-test: cannot mount devtmpfs\n");
		return 1;
	}

	/* a GPU probes in the background: wait until a card with that driver
	   shows up */
	int fd = -1;
	for (int t = 0; t < 300 && fd < 0; t++) {
		for (int c = 0; c < 4 && fd < 0; c++) {
			char name[32] = "";
			struct drm_version v = { .name_len = sizeof name - 1, .name = name };

			snprintf(path, sizeof path, "/dev/dri/card%d", c);
			fd = lkl_sys_open(path, LKL_O_RDWR, 0);
			if (fd < 0)
				continue;
			if (lkl_sys_ioctl(fd, DRM_IOCTL_VERSION, (long)&v) < 0
			    || strcmp(name, driver)) {
				lkl_sys_close(fd);
				fd = -1;
			}
		}
		if (fd < 0)
			usleep(100000);
	}
	if (fd < 0) {
		fprintf(stderr, "kms-test: no %s card\n", driver);
		return 1;
	}
	printf("%s is %s\n", path, driver);
	ioc(fd, DRM_IOCTL_SET_MASTER, NULL, "SET_MASTER");

	struct drm_mode_card_res res = { 0 };
	uint32_t crtcs[16], conns[16], encs[16];
	if (ioc(fd, DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES") < 0)
		return 1;
	if (res.count_crtcs > 16 || res.count_connectors > 16 || res.count_encoders > 16)
		return 1;
	res.count_fbs = 0;
	res.crtc_id_ptr = (uintptr_t)crtcs;
	res.connector_id_ptr = (uintptr_t)conns;
	res.encoder_id_ptr = (uintptr_t)encs;
	if (ioc(fd, DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES") < 0)
		return 1;
	printf("%s: %u crtcs, %u connectors, %u encoders, up to %ux%u\n", path,
	       res.count_crtcs, res.count_connectors, res.count_encoders,
	       res.max_width, res.max_height);

	/* first connected connector, its first (preferred) mode */
	struct drm_mode_modeinfo modes[64];
	struct drm_mode_get_connector conn;
	uint32_t conn_id = 0;
	for (uint32_t i = 0; i < res.count_connectors && !conn_id; i++) {
		memset(&conn, 0, sizeof conn);
		conn.connector_id = conns[i];
		if (ioc(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn, "GETCONNECTOR") < 0)
			continue;
		printf("connector %u: type %u, %s, %u modes\n", conns[i],
		       conn.connector_type,
		       conn.connection == DRM_MODE_CONNECTED ? "connected" : "not connected",
		       conn.count_modes);
		if (conn.connection != DRM_MODE_CONNECTED || !conn.count_modes)
			continue;
		if (conn.count_modes > 64)
			conn.count_modes = 64;
		conn.count_props = conn.count_encoders = 0;
		conn.modes_ptr = (uintptr_t)modes;
		if (ioc(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn, "GETCONNECTOR") == 0)
			conn_id = conns[i];
	}
	if (!conn_id) {
		printf("no connected display\n");
		return 2;
	}
	struct drm_mode_modeinfo *m = &modes[0];
	printf("mode %s: %ux%u @ %u Hz\n", m->name, m->hdisplay, m->vdisplay, m->vrefresh);

	struct drm_mode_get_encoder enc = { .encoder_id = conn.encoder_id };
	uint32_t crtc_id = crtcs[0];
	if (conn.encoder_id && ioc(fd, DRM_IOCTL_MODE_GETENCODER, &enc, "GETENCODER") == 0
	    && enc.crtc_id)
		crtc_id = enc.crtc_id;

	struct drm_mode_create_dumb db = { .width = m->hdisplay, .height = m->vdisplay, .bpp = 32 };
	if (ioc(fd, DRM_IOCTL_MODE_CREATE_DUMB, &db, "CREATE_DUMB") < 0)
		return 1;
	struct drm_mode_fb_cmd fb = { .width = db.width, .height = db.height, .pitch = db.pitch,
				      .bpp = 32, .depth = 24, .handle = db.handle };
	if (ioc(fd, DRM_IOCTL_MODE_ADDFB, &fb, "ADDFB") < 0)
		return 1;
	struct drm_mode_map_dumb map = { .handle = db.handle };
	if (ioc(fd, DRM_IOCTL_MODE_MAP_DUMB, &map, "MAP_DUMB") < 0)
		return 1;
	uint32_t *px = lkl_sys_mmap(NULL, db.size, LKL_PROT_READ | LKL_PROT_WRITE,
				    LKL_MAP_SHARED | LKL_MAP_POPULATE, fd, map.offset);
	uint32_t *px2 = lkl_sys_mmap(NULL, db.size, LKL_PROT_READ | LKL_PROT_WRITE,
				     LKL_MAP_SHARED | LKL_MAP_POPULATE, fd, map.offset);
	if ((long)px < 0 && (long)px > -4096) {
		fprintf(stderr, "kms-test: mmap: %s\n", lkl_strerror((long)px));
		return 1;
	}
	printf("buffer %ux%u pitch %u, %llu bytes at %p (and %p)\n", db.width, db.height,
	       db.pitch, (unsigned long long)db.size, (void *)px, (void *)px2);

	/* LKL has no page faults for these mappings: MAP_POPULATE fills them in
	   up front */
	/* colour bars */
	static const uint32_t bars[8] = { 0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
					  0xff00ff, 0xff0000, 0x0000ff, 0x000000 };
	for (uint32_t y = 0; y < db.height; y++)
		for (uint32_t x = 0; x < db.width; x++)
			px[y * (db.pitch / 4) + x] = bars[x * 8 / db.width];
	int same = (long)px2 > 0 && px2[db.pitch / 4 * (db.height / 2) + db.width - 1] == bars[7]
		&& px2[0] == bars[0];
	printf("second mapping %s\n", same ? "sees the same pixels" : "DIFFERS");

	struct drm_mode_crtc set = { .set_connectors_ptr = (uintptr_t)&conn_id,
				     .count_connectors = 1, .crtc_id = crtc_id,
				     .fb_id = fb.fb_id, .mode_valid = 1, .mode = *m };
	if (ioc(fd, DRM_IOCTL_MODE_SETCRTC, &set, "SETCRTC") < 0)
		return 1;
	printf("crtc %u shows framebuffer %u\n", crtc_id, fb.fb_id);
	sleep(argc > 3 ? 10 : 1);
	lkl_sys_halt();
	return same ? 0 : 1;
}
