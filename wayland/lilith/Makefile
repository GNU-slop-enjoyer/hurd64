.POSIX:
.SUFFIXES:

include config.mk

VPATH = src

LILITHCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
LILITHDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

PKGS      = wayland-server xkbcommon libdrm fcft pixman-1 $(XLIBS)
LILITHCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(LILITHCPPFLAGS) $(LILITHDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

# 9cynth is a normal wayland client so it gets its own flags
TUNEPKGS = wayland-client wayland-cursor xkbcommon pangocairo cairo glib-2.0 \
	libavformat libavcodec libavutil libswresample libswscale libpulse-simple epoll-shim
TUNECFLAGS = -I. -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	`$(PKG_CONFIG) --cflags $(TUNEPKGS)` $(CFLAGS)

all: lilith 9cynth
lilith: lilith.o util.o hurd96.o
	$(CC) lilith.o util.o hurd96.o $(LILITHCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
lilith.o: lilith.c hurd96.h client.h wallpaper.h bar.h rio.h riostats.h stb_image.h config.h config.mk \
	config/config_appearance.h config/config_animation.h config/config_bar.h \
	config/config_input.h config/config_keys.h cursor-shape-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	tablet-v2-protocol.h
util.o: util.c util.h
hurd96.o: hurd96.c hurd96.h xdg-shell-protocol.h
	$(CC) -D_GNU_SOURCE $(CPPFLAGS) $(LILITHCFLAGS) -Wno-pedantic -Wno-declaration-after-statement -o $@ -c src/hurd96.c
9cynth: 9cynth.c xdg-shell-client-protocol.h xdg-shell-protocol.c
	$(CC) $(TUNECFLAGS) $(LDFLAGS) -o $@ src/9cynth.c xdg-shell-protocol.c \
		`$(PKG_CONFIG) --libs $(TUNEPKGS)` -lm

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
tablet-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/unstable/tablet/tablet-unstable-v2.xml $@
xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
clean:
	rm -f lilith 9cynth *.o *-protocol.h xdg-shell-protocol.c lilith-*.tar.gz

dist: clean
	mkdir -p lilith-$(VERSION)
	mkdir -p lilith-$(VERSION)/src
	cp -R LICENSE* Makefile config.h config \
		config.mk protocols lilith.desktop 9cynth.desktop \
		lilith-$(VERSION)
	cp -R src/hurd96.c src/hurd96.h src/lilith.c src/client.h src/wallpaper.h src/bar.h src/rio.h src/riostats.h src/9cynth.c src/stb_image.h src/util.c src/util.h lilith-$(VERSION)/src/
	tar -caf lilith-$(VERSION).tar.gz lilith-$(VERSION)

	rm -rf lilith-$(VERSION)

install: lilith 9cynth
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/lilith $(DESTDIR)$(PREFIX)/bin/9cynth
	cp -f lilith 9cynth $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/lilith $(DESTDIR)$(PREFIX)/bin/9cynth
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f lilith.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/lilith.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/lilith.desktop
	mkdir -p $(DESTDIR)$(DATADIR)/applications
	cp -f 9cynth.desktop $(DESTDIR)$(DATADIR)/applications/9cynth.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/applications/9cynth.desktop
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lilith $(DESTDIR)$(PREFIX)/bin/9cynth \
		$(DESTDIR)$(DATADIR)/wayland-sessions/lilith.desktop \
		$(DESTDIR)$(DATADIR)/applications/9cynth.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(LILITHCFLAGS) -o $@ -c $<
