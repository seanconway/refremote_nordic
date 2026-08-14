cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote_dummy)

# Same binding decision as dongle/CMakeLists.txt and remote/CMakeLists.txt,
# same reasoning: identity is baked in at build time by
# dongle/tools/provision.py, no default to fall back to. PLAN.md §4.13.
if(NOT CONFIG_PROVISIONING_HEADER_DIR)
  message(FATAL_ERROR
    "CONFIG_PROVISIONING_HEADER_DIR not set. Generate one with dongle/tools/"
    "provision.py and pass -DCONFIG_PROVISIONING_HEADER_DIR=<dir containing "
    "provisioning_data.h>.")
endif()
if(NOT EXISTS "${CONFIG_PROVISIONING_HEADER_DIR}/provisioning_data.h")
  message(FATAL_ERROR "No provisioning_data.h in ${CONFIG_PROVISIONING_HEADER_DIR}")
endif()
target_include_directories(app PRIVATE ${CONFIG_PROVISIONING_HEADER_DIR})

# ../remote/src/link.c is compiled here UNCHANGED, by source reference, not by
# copy — PLAN.md §4.9: this board's whole reason for existing is to validate
# the *central's* two-connection scheduling with real remote firmware, not a
# thrown-away stand-in that diverges from what M5 extends. link.c has no DK-
# specific code in it at all (no GPIO, no board references) — every board
# dependency it has is through haptic.c/indicators.c's function signatures,
# which this app supplies its own PCA10059-shaped implementations of below.
# Same reasoning as common/ being shared between dongle and remote: one file,
# compiled into two applications, rather than a second copy to keep in step.
target_sources(app PRIVATE
  src/main.c
  src/buttons.c
  src/haptic.c
  src/indicators.c
  ../remote/src/link.c
  ../common/provisioning.c
  ../common/rframe.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../dongle/src)

# The headers this app builds against — link.h, buttons.h, haptic.h,
# indicators.h — are all pure interface (no DK-specific content anywhere in
# them, confirmed by reading them before reusing them), so they are reused
# from remote/src/ unchanged rather than duplicated. Only the four .c files
# above are new: main.c/buttons.c/haptic.c/indicators.c implement those same
# declared functions against this board's one button and two-LED hardware
# instead of the DK's seven-button, four-LED layout. link.c's own #include
# "haptic.h" resolves against its own directory (remote/src/) automatically —
# this path exists so THIS app's new .c files, which have no local copies,
# can find the same headers.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../remote/src)
