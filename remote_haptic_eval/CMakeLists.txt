cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote_haptic_eval)

# DRV2605L driver is shared source with remote/ -- CMake source reference,
# not a copy, same convention remote/CMakeLists.txt uses for common/. No
# provisioning, BLE or link.c here: this app never leaves the bench and
# never touches the radio, so none of that machinery is linked in.
target_sources(app PRIVATE
  src/main.c
  ../remote/src/drv2605.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../remote/src)
