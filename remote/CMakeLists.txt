cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote)

target_sources(app PRIVATE
  src/main.c
  src/link.c
  src/buttons.c
  src/haptic.c
  src/indicators.c
  src/prov_flash.c
  ../common/provisioning.c
  ../common/rframe.c
)

# remote/BUILD_SPEC.md §3: common/ is shared with the dongle by CMake source
# reference, not by copying — one codec, compiled into both firmwares and
# into the host suite.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# ../common/rframe.h reuses protocol.h's button/gesture/remote/waveform/
# indicator enums as its own vocabulary (dongle/CMakeLists.txt does the same
# for the identical reason). protocol.c itself is never linked here — only
# the enum declarations are needed, never the WP v3.0 line assembler.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../dongle/src)
