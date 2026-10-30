# Wayland on Hurd64

What runs: libwayland 1.24, wlroots 0.18.2, foot 1.21, and hurdwm, a tiling
compositor of our own. `desk` on the console starts it.

| piece | how |
|---|---|
| `epoll-shim/` | epoll, timerfd, signalfd, eventfd for the Hurd on poll() and socketpairs; `make test` |
| libwayland | `../patches/wayland/wayland-hurd.patch`: use epoll-shim on `gnu`, peer credentials = own user |
| libdrm 2.4.124 | `../patches/wayland/drm-hurd.patch`: `__GNU__` branch, Linux ioctl numbers, PATH_MAX (only headers/formats are used) |
| wlroots 0.18.2 | unpatched: `-Dbackends= -Drenderers= -Dallocators= -Dsession=disabled -Dxwayland=disabled -Dwerror=false`, `linux/input-event-codes.h` copied to /usr/local/include |
| foot 1.21 | `../patches/wayland/foot-hurd.patch` (no SO_DOMAIN) |
| `hurdwm/` | headless backend + pixman, frames copied to GNU Mach's `fb` device; keys from tuxkbd |

Build order in the build VM (`vm/build-vm.sh`), everything to /usr/local:
epoll-shim (`make install`), wayland, drm, wlroots (meson), foot, hurdwm (`make install`).
`vm/update-wayland.sh` copies the result into the install image with the Debian
runtime libraries.

hurdwm keys: Super+Enter terminal, Super+J/K/Tab focus, Super+Shift+Enter to the
big tile, Super+F full screen, Super+Q close, Super+P screenshot, Super+Esc quit.
