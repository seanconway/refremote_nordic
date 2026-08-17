cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote)

# Same binding decision as dongle/CMakeLists.txt, same reasoning: identity is
# baked in at build time by dongle/tools/provision.py, no default to fall
# back to. PLAN.md §4, 2026-08-12.
if(NOT CONFIG_PROVISIONING_HEADER_DIR)
  message(FATAL_ERROR
    "CONFIG_PROVISIONING_HEADER_DIR not set. Generate one with dongle/tools/"
    "provision.py and pass -DCONFIG_PROVISIONING_HEADER_DIR=<dir containing "
    "provisioning_data.h>. See remote/BUILD_SPEC.md §9.")
endif()
if(NOT EXISTS "${CONFIG_PROVISIONING_HEADER_DIR}/provisioning_data.h")
  message(FATAL_ERROR "No provisioning_data.h in ${CONFIG_PROVISIONING_HEADER_DIR}")
endif()
target_include_directories(app PRIVATE ${CONFIG_PROVISIONING_HEADER_DIR})

target_sources(app PRIVATE
  src/main.c
  src/link.c
  src/buttons.c
  src/haptic.c
  src/indicators.c
  src/drv2605.c
  ../common/provisioning.c
  ../common/rframe.c
  ../common/ind_colour.c
)

# DRV2605L breadboard bring-up (bench-only) -- see remote/Kconfig.
target_sources_ifdef(CONFIG_REMOTE_DRV2605_CAL_TEST app PRIVATE src/drv2605_cal_test.c)

# remote/BUILD_SPEC.md §3: common/ is shared with the dongle by CMake source
# reference, not by copying — one codec, compiled into both firmwares and
# into the host suite.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ../common/rframe.h reuses protocol.h's button/gesture/remote/waveform/
# indicator enums as its own vocabulary (dongle/CMakeLists.txt does the same
# for the identical reason). protocol.c itself is never linked here — only
# the enum declarations are needed, never the WP v3.0 line assembler.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../dongle/src)
