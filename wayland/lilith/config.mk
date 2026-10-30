VERSION  = 1

PKG_CONFIG = PKG_CONFIG_PATH=/usr/local/lib/x86_64-gnu/pkgconfig:/usr/local/lib/pkgconfig pkg-config

PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

WLR_INCS = `$(PKG_CONFIG) --cflags wlroots-0.19`
WLR_LIBS = `$(PKG_CONFIG) --libs wlroots-0.19`

XWAYLAND = -DXWAYLAND
XLIBS = xcb xcb-icccm

CC = cc
CFLAGS = -O2 -DPATH_MAX=4096
