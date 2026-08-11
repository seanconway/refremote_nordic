#!/usr/bin/env bash
#
# Puts a *host* C compiler on PATH so the Zephyr-free unit suites can run.
#
#   source dongle/tools/hostenv.sh
#   cd dongle/tests/protocol && make check
#
# This is a different toolchain from ncsenv.sh and the two are not
# interchangeable. `west build` uses arm-zephyr-eabi-gcc, which cross-compiles
# for the nRF52840 and emits binaries this machine cannot execute. protocol.c,
# rframe.c and provisioning.c are deliberately Zephyr-free precisely so they can
# also be built *natively* and run here, with no board and no SDK — and that
# needs a compiler targeting Windows, which the NCS bundle does not contain.
#
# Assuming the board toolchain could stand in for a host one is what left the
# parser suite unexecuted from M0 to M2. See PLAN.md §5 rung V0.
#
# Installed once with:
#   winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e
#
# If WinLibs is reinstalled or upgraded the package directory keeps its name, so
# this path should survive. Override HOSTCC_BIN if it does not.

# $HOME rather than $USER: Git Bash leaves USER unset, so the interpolated path
# came out as /c/Users//AppData/... and the check failed with a message that
# looked like a missing install rather than a missing variable.
HOSTCC_BIN="${HOSTCC_BIN:-$HOME/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin}"

if [ ! -x "$HOSTCC_BIN/gcc.exe" ]; then
	echo "hostenv: no host gcc at $HOSTCC_BIN" >&2
	echo "hostenv: install with 'winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e'" >&2
	echo "hostenv: or set HOSTCC_BIN to an existing MinGW-w64 bin directory" >&2
	return 1 2>/dev/null || exit 1
fi

# Prepended, not appended: if ncsenv.sh was sourced first its PATH leads with the
# NCS bundle, and a build that silently picked up a cross-compiler would produce
# a test binary that will not run — with no error until execution.
export PATH="$HOSTCC_BIN:$PATH"

# WinLibs ships GNU make as mingw32-make and there is no `make` anywhere on this
# machine — not in Git Bash, not in the NCS bundle. The shim keeps the documented
# command (`make check`) honest rather than forking the instructions per host.
if ! command -v make >/dev/null 2>&1; then
	make() { mingw32-make "$@"; }
fi

echo "hostenv: $(gcc --version | head -1)"
