cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dongle)

target_sources(app PRIVATE
  src/main.c
  src/protocol.c
  src/usb_link.c
  src/engine.c
  src/indicator.c
  src/provisioning_flash.c
  ../common/provisioning.c
)

# Stage 2 (BUILD_SPEC.md §9): the record's shape lives in ../common, shared
# unchanged with the remote firmware, so it is not duplicated under src/.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Stage 3: ../common/rframe.h reuses protocol.h's button/gesture/remote/
# waveform/indicator enums as its own vocabulary (radio.h has done this since
# Stage 1 — "the engine never acquires a second name for something the wire
# protocol already names"), so a common/ header needs src/ on its include
# path to reach a src/ header. protocol.c itself is never linked into the
# remote firmware; only the enum declarations are needed there.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Exactly one implementation of radio.h is compiled in.
#
# rframe.c is scoped to the y branch, not the unconditional list above:
# radio_null.c never touches a frame (BUILD_SPEC §2 — "everything DISCONNECTED,
# rendered on the board's two LEDs"), so linking the codec into the no-radio
# baseline would be dead weight with no caller, the same reasoning that keeps
# provisioning_flash.c's role check strict rather than permissive.
if(CONFIG_DONGLE_RADIO)
  target_sources(app PRIVATE src/radio_ble.c ../common/rframe.c)
else()
  target_sources(app PRIVATE src/radio_null.c)
endif()
