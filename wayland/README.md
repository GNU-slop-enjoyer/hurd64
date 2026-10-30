# Wayland on hurd96

What runs: libwayland 1.24, wlroots 0.19.3, foot 1.21, and lilith, a floating
compositor with an endless canvas (<https://codeberg.org/Yuas/lilith-fork>),
with its music player 9cynth. `desk` on the console starts it.

| piece | how |
|---|---|
| `epoll-shim/` | epoll, timerfd, signalfd, eventfd for the Hurd on poll() and socketpairs; `make test` |
| libwayland | `../patches/wayland/wayland-hurd.patch`: use epoll-shim on `gnu`, peer credentials = own user |
| libdrm 2.4.124 | `../patches/wayland/drm-hurd.patch`: `__GNU__` branch, Linux ioctl numbers, PATH_MAX (only headers/formats are used) |
| wlroots 0.19.3 | unpatched: `-Dbackends= -Drenderers= -Dallocators= -Dsession=disabled -Dxwayland=enabled -Dwerror=false -Dexamples=false`, `linux/input-event-codes.h` copied to /usr/local/include |
| foot 1.21 | `../patches/wayland/foot-hurd.patch` (no SO_DOMAIN) |
| Xwayland 24.1.6 | `../patches/wayland/xwayland-hurd.patch`: epoll-shim on `gnu`, no LocalAccessScopeUser without peer credentials; `-Dglamor=false -Dglx=false -Ddrm=false -Dsecure-rpc=false -Dxdmcp=false -Dlisten_tcp=false -Dxwayland_ei=false -Dlibdecor=false`; `linux/input.h` = a stub including input-event-codes.h |
| `lilith/` | lilith-fork with `__GNU__` branches: headless backend + pixman, frames copied to GNU Mach's `fb` device, input from hurd96kbd as a real wlr_keyboard and wlr_pointer (`src/hurd96.c`), no libinput, software cursors locked on (the headless output only pretends to have a hardware one). 9cynth: pulse only (no ALSA on the Hurd), shm_open for memfd_create |

Build order in the build VM (`vm/build-vm.sh`), everything to /usr/local:
epoll-shim (`make install`), wayland, drm, wlroots (meson), foot, then
`vm/update-desk.sh` builds lilith and 9cynth and puts all of it into the install
image with the Debian runtime libraries.

Using it: a new window waits for you to sweep a box for it with the mouse.
Win+Q terminal, right click on the canvas for rio's menu, Win+R launcher
(dmenu through Xwayland), Win+M 9cynth, Win+C close, Win+Z fit all windows,
Win+Shift+Q back to the console. `lilith/README.md` has the rest.

There is no sound driver on hurd96 yet, so 9cynth shows "no sound" unless
NINECYNTH_AUDIO=null is set.
