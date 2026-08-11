# Host build of the protocol unit tests. Needs nothing but a C compiler —
# protocol.c has no Zephyr dependencies, which is the whole point (PROTOCOL.md §11).
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

test_protocol: test_protocol.c ../../src/protocol.c ../../src/protocol.h
	$(CC) $(CFLAGS) -o $@ test_protocol.c ../../src/protocol.c

.PHONY: check clean
check: test_protocol
	./test_protocol

clean:
	rm -f test_protocol test_protocol.exe
