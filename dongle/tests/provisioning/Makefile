# Host build of the provisioning record unit tests. Needs nothing but a C
# compiler — common/provisioning.c has no Zephyr dependencies, same reasoning
# as tests/protocol (PROTOCOL.md §11, dongle/BUILD_SPEC.md §2.1).
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

test_provisioning: test_provisioning.c ../../../common/provisioning.c ../../../common/provisioning.h
	$(CC) $(CFLAGS) -o $@ test_provisioning.c ../../../common/provisioning.c

.PHONY: check clean
check: test_provisioning
	./test_provisioning

clean:
	rm -f test_provisioning test_provisioning.exe
