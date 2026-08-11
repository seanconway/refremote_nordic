cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dongle)

target_sources(app PRIVATE
  src/main.c
  src/protocol.c
  src/usb_link.c
  src/engine.c
  src/indicator.c
)

# Exactly one implementation of radio.h is compiled in. Selecting neither would
# fail at link time with an undefined-reference list rather than a sentence, so
# the y case is refused here until src/radio_ble.c lands at Stage 3.
if(CONFIG_DONGLE_RADIO)
  message(FATAL_ERROR
    # Plain ASCII: the CMake console mangles a section sign into mojibake, and
    # a build-stopping message is the wrong place to lose a document reference.
    "CONFIG_DONGLE_RADIO=y needs src/radio_ble.c, which arrives at Stage 3 "
    "(PLAN.md section 3.1). Build the no-radio baseline with "
    "-DCONFIG_DONGLE_RADIO=n, which is the current default.")
else()
  target_sources(app PRIVATE src/radio_null.c)
endif()
