# hurdwm, on the Hurd: make && make install
PREFIX ?= /usr/local
.DEFAULT_GOAL := hurdwm
PKGS = wlroots-0.18 wayland-server xkbcommon pixman-1 libdrm
export PKG_CONFIG_PATH := /usr/local/lib/x86_64-gnu/pkgconfig:$(PKG_CONFIG_PATH)
CFLAGS ?= -O2 -g
CFLAGS += -Wall -I. -DWLR_USE_UNSTABLE $(shell pkg-config --cflags $(PKGS))
LDLIBS = $(shell pkg-config --libs $(PKGS))
OBJ = hurdwm.o screen.o keys.o bar.o
WP = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
PROTO = xdg-shell-protocol.h xdg-decoration-unstable-v1-protocol.h

# wlroots' headers include these server-side protocol headers
xdg-shell-protocol.h:
	wayland-scanner server-header $(WP)/stable/xdg-shell/xdg-shell.xml $@
xdg-decoration-unstable-v1-protocol.h:
	wayland-scanner server-header $(WP)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml $@

hurdwm: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): screen.h keys.h bar.h $(PROTO)

install: hurdwm
	install -m 755 hurdwm $(DESTDIR)$(PREFIX)/bin/hurdwm

clean:
	rm -f hurdwm $(OBJ) $(PROTO)
