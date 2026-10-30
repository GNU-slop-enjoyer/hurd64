# tuxdesk. On the Hurd: make LKL=/build/lkl (needs ../lkl-hurd built first).
# On Linux, for a quick try with vkms: make linux
LKL ?= /build/lkl
CFLAGS = -O2 -g -Wall -I$(LKL)/include
SRC = desk.c term.c kms.c fb.c input.c

tuxdesk: $(SRC) kms.h term.h input.h font8x16.h ../lkl-hurd/liblkl-hurd.a
	$(CC) $(CFLAGS) -o $@ $(SRC) -Wl,--whole-archive ../lkl-hurd/liblkl-hurd.a \
		-Wl,--no-whole-archive -lpciaccess -lirqhelp -lpthread -lrt

LINUX_LKL = $(HOME)/src/hurd64/lkl/tools/lkl
linux: $(SRC)
	$(CC) -O2 -g -Wall -I$(LINUX_LKL)/include -o tuxdesk-linux $(SRC) $(LINUX_LKL)/liblkl.a -lpthread -lrt

clean:
	rm -f tuxdesk tuxdesk-linux
