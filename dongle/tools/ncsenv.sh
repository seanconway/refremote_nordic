#!/usr/bin/env bash
#
# Reconstructs the nRF Connect SDK v3.4.0 build environment so `west` works
# from a plain shell, without the VS Code extension.
#
#   source dongle/tools/ncsenv.sh
#   west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
#
# The variable set below mirrors the toolchain's own environment.json. If the
# toolchain is reinstalled the hash directory changes — update NCS_TOOLCHAIN.

NCS_TOOLCHAIN="${NCS_TOOLCHAIN:-/c/ncs/toolchains/dcbdc366a1}"
NCS_SDK="${NCS_SDK:-/c/ncs/v3.4.0}"

if [ ! -d "$NCS_TOOLCHAIN" ]; then
	echo "ncsenv: toolchain not found at $NCS_TOOLCHAIN" >&2
	echo "ncsenv: set NCS_TOOLCHAIN to the correct path and re-source" >&2
	return 1 2>/dev/null || exit 1
fi

export PATH="$NCS_TOOLCHAIN:$NCS_TOOLCHAIN/mingw64/bin:$NCS_TOOLCHAIN/bin:$NCS_TOOLCHAIN/opt/bin:$NCS_TOOLCHAIN/opt/bin/Scripts:$NCS_TOOLCHAIN/opt/nanopb/generator-bin:$NCS_TOOLCHAIN/nrfutil/bin:$NCS_TOOLCHAIN/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$NCS_TOOLCHAIN/opt/zephyr-sdk/gnu/riscv64-zephyr-elf/bin:$PATH"
export PYTHONPATH="$NCS_TOOLCHAIN/opt/bin;$NCS_TOOLCHAIN/opt/bin/Lib;$NCS_TOOLCHAIN/opt/bin/Lib/site-packages"
export NRFUTIL_HOME="$NCS_TOOLCHAIN/nrfutil/home"
export ZEPHYR_TOOLCHAIN_VARIANT="zephyr/gnu"
export ZEPHYR_SDK_INSTALL_DIR="$NCS_TOOLCHAIN/opt/zephyr-sdk"
export ZEPHYR_BASE="$NCS_SDK/zephyr"

# west ships as a Python module in the bundle rather than a standalone exe.
west() { "$NCS_TOOLCHAIN/opt/bin/python.exe" -m west "$@"; }
