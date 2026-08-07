# Host build of the protocol unit tests. Needs nothing but a C compiler —
# protocol.c has no Zephyr dependencies, which is the whole point (PROTOCOL.md §11).
#
#   make && ./test_protocol
#
# Works under Linux, macOS, WSL, or MinGW/MSYS2 on Windows.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O1 -g

test_protocol: test_protocol.c ../../src/protocol.c ../../src/protocol.h
	$(CC) $(CFLAGS) -o $@ test_protocol.c ../../src/protocol.c

.PHONY: check clean
check: test_protocol
	./test_protocol

clean:
	rm -f test_protocol test_protocol.exe
