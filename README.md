# hurd96

Work toward a 64-bit, multicore GNU/Hurd that runs on real x86-64 hardware.
GNU Mach and the Hurd stay the foundation. Upstream sources are used as-is
where they work, and changes are kept as small patches against upstream.

Start with [PROJECT_STATUS.md](PROJECT_STATUS.md). What to do next is in
[NEXT_SESSION.md](NEXT_SESSION.md).

- `utils/`   small Hurd tools (smpctl)
- `vm/`      scripts for the QEMU test VM (Debian GNU/Hurd amd64 image as the base)
- `patches/` changes against upstream gnumach / hurd
- `docs/`    building, testing, hardware, architecture notes

License: GPL-2.0-or-later, like GNU Mach and the Hurd.
