/* The few Linux DRM/KMS definitions kms-test needs (from include/uapi/drm).
 * Spelled out because these go to Linux inside LKL: the ioctl numbers must
 * use Linux's encoding, which is not the Hurd's. */
#ifndef LINUX_DRM_H
#define LINUX_DRM_H
#include <stdint.h>

#define L_IOC(dir, nr, size)	(((dir) << 30) | ((size) << 16) | ('d' << 8) | (nr))
#define L_IO(nr)		L_IOC(0, nr, 0)
#define L_IOWR(nr, type)	L_IOC(3, nr, sizeof(type))

struct drm_mode_card_res {
	uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
	uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
	uint32_t min_width, max_width, min_height, max_height;
};

struct drm_mode_modeinfo {
	uint32_t clock;
	uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
	uint32_t vrefresh, flags, type;
	char name[32];
};

struct drm_mode_crtc {
	uint64_t set_connectors_ptr;
	uint32_t count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
	struct drm_mode_modeinfo mode;
};

struct drm_mode_get_encoder {
	uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
};

struct drm_mode_get_connector {
	uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
	uint32_t count_modes, count_props, count_encoders;
	uint32_t encoder_id, connector_id, connector_type, connector_type_id;
	uint32_t connection, mm_width, mm_height, subpixel, pad;
};

struct drm_mode_fb_cmd {
	uint32_t fb_id, width, height, pitch, bpp, depth, handle;
};

struct drm_mode_create_dumb {
	uint32_t height, width, bpp, flags, handle, pitch;
	uint64_t size;
};

struct drm_mode_map_dumb {
	uint32_t handle, pad;
	uint64_t offset;
};

#define DRM_IOCTL_SET_MASTER		L_IO(0x1e)
#define DRM_IOCTL_MODE_GETRESOURCES	L_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_SETCRTC		L_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER	L_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR	L_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB		L_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_CREATE_DUMB	L_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB		L_IOWR(0xB3, struct drm_mode_map_dumb)

#define DRM_MODE_CONNECTED	1
#endif
