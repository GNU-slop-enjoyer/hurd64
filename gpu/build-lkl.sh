#!/bin/sh
# Build LKL (Linux as a library, github.com/lkl/linux) with amdgpu, on the
# Linux host. The result, lkl.o plus generated headers, goes to the Hurd
# build VM, where the host library (POSIX + lkl-hurd/hurd_host.c) is built.
#
#   gpu/build-lkl.sh            uses ~/src/hurd64/lkl
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=${LKL_SRC:-$HOME/src/hurd64/lkl}
out=${LKL_OUT:-$HOME/src/hurd64/build/lkl}

[ -d "$src/arch/lkl" ] || git clone --depth 1 https://github.com/lkl/linux.git "$src"
cd "$src"
if [ -e arch/lkl/include/asm/fpu.h ]; then
	echo "patch already applied"
else
	git apply "$here/../patches/lkl/0001-kernel-fpu-and-amdgpu-config.patch"
fi
mkdir -p "$out"
make -C tools/lkl MMU=1 KCONFIG=tux_defconfig -j"$(nproc)" lib/lkl.o 2>&1 | tail -20
cp tools/lkl/lib/lkl.o "$out/"
rm -rf "$out/include" && cp -r tools/lkl/include "$out/include"
ls -la "$out/lkl.o"
