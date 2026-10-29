/* vfct-extract: write the VBIOS images in an ACPI VFCT table to files.
 *
 *   vfct-extract VFCT [outdir]     writes outdir/vbios_<device>.bin
 *
 * UEFI firmware hands the GPU's video BIOS to the OS in the VFCT table
 * (/sys/firmware/acpi/tables/VFCT, root only). amdgpu inside LKL has no
 * ACPI, so it loads the image as firmware amdgpu/vbios_<device>.bin.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ACPI header (36 bytes), then the VFCT fields */
struct vfct {
	char sig[4];
	uint32_t length;
	uint8_t rev, sum;
	char oem[6], oem_table[8];
	uint32_t oem_rev, creator, creator_rev;
	uint8_t table_uuid[16];
	uint32_t vbios_offset, lib1_offset, reserved[4];
} __attribute__((packed));

struct vfct_image {
	uint32_t bus, device, function;
	uint16_t vendor, device_id, ss_vendor, ss_device;
	uint32_t revision, length;
} __attribute__((packed));

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: vfct-extract VFCT [outdir]\n");
		return 2;
	}
	const char *dir = argc > 2 ? argv[2] : ".";
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 1;
	}
	static uint8_t buf[1 << 22];
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);

	struct vfct *t = (struct vfct *)buf;
	if (n < sizeof *t || memcmp(t->sig, "VFCT", 4)) {
		fprintf(stderr, "%s: not a VFCT table\n", argv[1]);
		return 1;
	}
	if (t->length < n)
		n = t->length;
	int found = 0;
	for (size_t off = t->vbios_offset; off + sizeof(struct vfct_image) <= n;) {
		struct vfct_image *im = (struct vfct_image *)(buf + off);
		size_t data = off + sizeof *im;

		if (!im->length || data + im->length > n)
			break;
		char path[4096];
		snprintf(path, sizeof path, "%s/vbios_%04x.bin", dir, im->device_id);
		FILE *o = fopen(path, "wb");
		if (!o || fwrite(buf + data, 1, im->length, o) != im->length || fclose(o)) {
			perror(path);
			return 1;
		}
		printf("%02x:%02x.%x %04x:%04x  %u bytes -> %s\n", im->bus, im->device,
		       im->function, im->vendor, im->device_id, im->length, path);
		found++;
		off = data + im->length;
	}
	if (!found) {
		fprintf(stderr, "no VBIOS images in %s\n", argv[1]);
		return 1;
	}
	return 0;
}
