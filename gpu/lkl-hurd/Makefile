# Builds LKL's host library and the test program on the Hurd, against lkl.o
# and headers from gpu/build-lkl.sh (the kernel itself is built on Linux).
#   make LKL=/build/lkl
LKL ?= /build/lkl
CFLAGS = -O2 -g -fPIC -pthread -Wall -Wno-unused-parameter \
	-D_FILE_OFFSET_BITS=64 -DPTHREAD_KEYS_MAX=1024 \
	-I$(LKL)/include -iquote $(LKL)/lib
LIBSRC = fs iomem jmp_buf posix-host utils virtio
OBJS = $(LIBSRC:%=lib-%.o) hurd_host.o

all: liblkl-hurd.a lkl-boot

# the generated one describes the Linux host (VFIO, FUSE, ...)
$(OBJS): $(LKL)/include/lkl_autoconf.h
$(LKL)/include/lkl_autoconf.h: lkl_autoconf.h
	cp $< $@

# the Hurd has no PTHREAD_KEYS_MAX (no limit); posix-host.c sizes a table with it
lib-%.o: $(LKL)/lib/%.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -c -o $@ $<

hurd_host.o: hurd_host.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -c -o $@ $<

liblkl-hurd.a: $(OBJS) $(LKL)/lkl-s.o
	rm -f $@ && ar rcs $@ $^

# hurd_host.o carries a constructor and nothing else references it
lkl-boot: $(LKL)/test/lkl-boot.c liblkl-hurd.a
	$(CC) $(CFLAGS) -o $@ $< -Wl,--whole-archive liblkl-hurd.a -Wl,--no-whole-archive -lpciaccess -lirqhelp -lpthread -lrt

clean:
	rm -f *.o liblkl-hurd.a lkl-boot
