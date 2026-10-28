# Building

Host: Gentoo x86_64, gcc. Paths below are under ~/src/hurd64.

    # GNU Mach headers, then MIG (needs the headers), then the kernel
    T=$PWD/tools
    (cd upstream/gnumach && autoreconf -fi)
    mkdir -p build/gnumach-hdr && cd build/gnumach-hdr
    ../../upstream/gnumach/configure --host=x86_64-gnu --prefix=$T CC=gcc LD=ld MIG=true
    make install-data && cd ../..
    (cd upstream/mig && autoreconf -fi)
    mkdir -p build/mig && cd build/mig
    ../../upstream/mig/configure --target=x86_64-gnu --prefix=$T TARGET_CC=gcc TARGET_CPPFLAGS=-I$T/include
    make -j8 && make install && cd ../..
    mkdir -p build/gnumach-smp && cd build/gnumach-smp
    ../../upstream/gnumach/configure --host=x86_64-gnu CC=gcc LD=ld MIG=$T/bin/x86_64-gnu-mig \
        --enable-ncpus=64 --enable-apic --enable-kdb --disable-user32
    make -j8 gnumach.gz

`utils/smpctl.c` is built inside the Hurd VM: `gcc -O2 -o /usr/local/sbin/smpctl smpctl.c`.
