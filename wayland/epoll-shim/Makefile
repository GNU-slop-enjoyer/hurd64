# epoll-shim for the Hurd: make && make install (into PREFIX, default /usr/local)
PREFIX ?= /usr/local
CFLAGS ?= -O2 -g
CFLAGS += -Wall -fPIC -pthread
VERSION = 0.1

all: libepoll-shim.so epoll-shim.pc

libepoll-shim.so: shim.c include/sys/*.h
	$(CC) $(CFLAGS) -shared -Wl,-soname,libepoll-shim.so.0 -o $@ shim.c -lpthread

epoll-shim.pc: Makefile
	printf 'prefix=%s\nlibdir=$${prefix}/lib\nincludedir=$${prefix}/include\n\nName: epoll-shim\nDescription: epoll, timerfd, signalfd and eventfd for the Hurd\nVersion: %s\nCflags: -I$${includedir}/libepoll-shim\nLibs: -L$${libdir} -lepoll-shim -lpthread\n' $(PREFIX) $(VERSION) > $@

install: all
	install -d $(DESTDIR)$(PREFIX)/lib/pkgconfig $(DESTDIR)$(PREFIX)/include/libepoll-shim/sys
	install -m 755 libepoll-shim.so $(DESTDIR)$(PREFIX)/lib/libepoll-shim.so.0
	ln -sf libepoll-shim.so.0 $(DESTDIR)$(PREFIX)/lib/libepoll-shim.so
	install -m 644 include/sys/*.h $(DESTDIR)$(PREFIX)/include/libepoll-shim/sys/
	install -m 644 epoll-shim.pc $(DESTDIR)$(PREFIX)/lib/pkgconfig/

test: test.c libepoll-shim.so
	$(CC) $(CFLAGS) -Iinclude -o $@ test.c -L. -lepoll-shim -lpthread

clean:
	rm -f libepoll-shim.so epoll-shim.pc test
