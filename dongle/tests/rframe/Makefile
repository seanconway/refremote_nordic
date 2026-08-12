# Host build of the frame codec unit tests. Needs nothing but a C compiler —
# common/rframe.c has no Zephyr dependencies, same reasoning as tests/protocol
# and tests/provisioning (RADIO_PROTOCOL.md, dongle/BUILD_SPEC.md §2.1).
#
#   source dongle/tools/hostenv.sh
#   make check
#
# Works under Linux, macOS, WSL, or MinGW-w64 on Windows.
#
# `CC ?= gcc` does NOT work here: make defines CC as `cc` among its built-in
# variables, so `?=` sees it as already set and the recipe invokes a compiler
# that exists on Unix and not on Windows. Testing $(origin) is the difference
# between "the user chose a compiler" and "make guessed one", and only the
# second should be overridden.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g

# rframe.h includes protocol.h for the shared button/gesture/waveform/
# indicator enums, but only as types — nothing here calls into protocol.c, so
# it is on the include path and not in the link.
SRCS := test_rframe.c ../../../common/rframe.c

test_rframe: $(SRCS) ../../../common/rframe.h ../../src/protocol.h
	$(CC) $(CFLAGS) -I../../../common -I../../src -o $@ $(SRCS)

.PHONY: check clean
check: test_rframe
	./test_rframe

clean:
	rm -f test_rframe test_rframe.exe
