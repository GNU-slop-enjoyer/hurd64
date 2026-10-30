#!/bin/sh
# Build LKL (Linux as a library, github.com/lkl/linux) with amdgpu, on the
# Linux host. The result, lkl.o plus generated headers, goes to the Hurd
# build VM, where the host library (POSIX + lkl-hurd/hurd_host.c) is built.
#
#   gpu/build-lkl.sh            uses ~/src/hurd96/lkl
set -eu
if [ "$(id -u)" = 0 ]; then echo "run as your user, not root" >&2; exit 1; fi
here=$(cd "$(dirname "$0")" && pwd)
src=${LKL_SRC:-$HOME/src/hurd96/lkl}
out=${LKL_OUT:-$HOME/src/hurd96/build/lkl}

# iGPU firmware (Granite Ridge: GC 10.3.6, PSP 13.0.5, SDMA 5.2.6, DCN 3.1.5, VCN 4.0.2)
fw="gc_10_3_6_ce gc_10_3_6_me gc_10_3_6_mec gc_10_3_6_mec2 gc_10_3_6_pfp gc_10_3_6_rlc
psp_13_0_5_asd psp_13_0_5_ta psp_13_0_5_toc sdma_5_2_6 dcn_3_1_5_dmcub vcn_4_0_2"

[ -d "$src/arch/lkl" ] || git clone --depth 1 https://github.com/lkl/linux.git "$src"
cd "$src"
if [ -e arch/lkl/include/asm/fpu.h ]; then
	echo "patch already applied"
else
	git apply "$here/../patches/lkl/0001-kernel-fpu-and-amdgpu-config.patch"
fi
mkdir -p "$out"

# built-in firmware, from a directory inside the tree (EXTRA_FIRMWARE_DIR is
# relative to it)
rm -rf firmware-hurd96 && mkdir -p firmware-hurd96/amdgpu
list=
for f in $fw; do
	cp "/lib/firmware/amdgpu/$f.bin" firmware-hurd96/amdgpu/
	list="$list amdgpu/$f.bin"
done
# the VBIOS from the ACPI VFCT table (gpu/igpu-vfio.sh copies it there)
if [ -r "$out/VFCT" ]; then
	cc -O2 -o "$out/vfct-extract" "$here/vfct-extract.c"
	"$out/vfct-extract" "$out/VFCT" firmware-hurd96/amdgpu
	for v in firmware-hurd96/amdgpu/vbios_*.bin; do
		list="$list amdgpu/${v##*/}"
	done
fi
{
	cat arch/lkl/configs/defconfig "$here/lkl-amdgpu.config"
	echo "CONFIG_EXTRA_FIRMWARE=\"${list# }\""
	echo 'CONFIG_EXTRA_FIRMWARE_DIR="firmware-hurd96"'
} > arch/lkl/configs/hurd96_defconfig.new
if cmp -s arch/lkl/configs/hurd96_defconfig.new arch/lkl/configs/hurd96_defconfig; then
	rm arch/lkl/configs/hurd96_defconfig.new
else
	mv arch/lkl/configs/hurd96_defconfig.new arch/lkl/configs/hurd96_defconfig
	rm -f .config		# regenerate it from the new defconfig
fi

# lowest priority: the machine stays usable while this builds
nice -n 19 make -C tools/lkl MMU=1 KCONFIG=hurd96_defconfig -j"${JOBS:-16}" "$src/tools/lkl/lib/lkl.o" >"$out/build.log" 2>&1 || { tail -30 "$out/build.log"; exit 1; }
cp tools/lkl/lib/lkl.o "$out/"
objcopy --strip-debug "$out/lkl.o" "$out/lkl-s.o"
rm -rf "$out/include" && cp -r tools/lkl/include "$out/include"
ls -la "$out/lkl.o" "$out/lkl-s.o"
