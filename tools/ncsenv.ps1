# Reconstructs the nRF Connect SDK v3.4.0 build environment so `west` and
# `python` work from a plain PowerShell window, without the VS Code
# extension. PowerShell translation of dongle/tools/ncsenv.sh — kept at the
# repo root rather than under dongle/, since it sets up the one toolchain
# both dongle/ and remote/ build against, not something dongle-specific.
#
#   . tools\ncsenv.ps1
#   west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
#
# Must be dot-sourced (the leading ". "), not run directly — a plain
# invocation runs in a child process and every environment variable set below
# would vanish the moment the script exits, leaving the calling window
# exactly as unconfigured as before.
#
# Deliberately scoped to sessions that opt in, rather than a $PROFILE entry
# that would apply to every PowerShell window on this machine, including ones
# with nothing to do with this project — the NCS toolchain's own bundled
# Python quietly shadowing whatever `python` should resolve to elsewhere is
# exactly the kind of failure that surfaces weeks later, in an unrelated
# session, with no obvious cause.
#
# The variable set below mirrors the toolchain's own environment.json. If the
# toolchain is reinstalled the hash directory changes — update $NCS_TOOLCHAIN.

if (-not $env:NCS_TOOLCHAIN) { $env:NCS_TOOLCHAIN = "C:\ncs\toolchains\dcbdc366a1" }
if (-not $env:NCS_SDK) { $env:NCS_SDK = "C:\ncs\v3.4.0" }

if (-not (Test-Path $env:NCS_TOOLCHAIN)) {
    Write-Error "ncsenv: toolchain not found at $env:NCS_TOOLCHAIN — set `$env:NCS_TOOLCHAIN and re-source"
    return
}

$env:PATH = "$env:NCS_TOOLCHAIN;$env:NCS_TOOLCHAIN\mingw64\bin;$env:NCS_TOOLCHAIN\bin;$env:NCS_TOOLCHAIN\opt\bin;$env:NCS_TOOLCHAIN\opt\bin\Scripts;$env:NCS_TOOLCHAIN\opt\nanopb\generator-bin;$env:NCS_TOOLCHAIN\nrfutil\bin;$env:NCS_TOOLCHAIN\opt\zephyr-sdk\gnu\arm-zephyr-eabi\bin;$env:NCS_TOOLCHAIN\opt\zephyr-sdk\gnu\riscv64-zephyr-elf\bin;$env:PATH"
$env:PYTHONPATH = "$env:NCS_TOOLCHAIN\opt\bin;$env:NCS_TOOLCHAIN\opt\bin\Lib;$env:NCS_TOOLCHAIN\opt\bin\Lib\site-packages"
# Deliberately NOT setting NRFUTIL_HOME. It looks like it belongs here —
# mirrors ZEPHYR_SDK_INSTALL_DIR, points at the toolchain's own nrfutil — but
# nrfutil uses it to find where a plugin's own data (including nrf5sdk-tools'
# legacy DFU helper, pc_nrfutil_legacy_v6.1.7.exe) is cached, and on this
# machine that plugin was only ever installed under the *personal*
# ~/.nrfutil, not the toolchain-scoped one this variable would point at.
# Setting it here silently redirects a working nrfutil install to a home
# directory where its own plugin was never set up: `nrfutil nrf5sdk-tools dfu
# usb-serial` fails with "pc_nrfutil_legacy_v6.1.7.exe not found in any of
# the search paths" — a real DFU error, not a PATH error, so it doesn't look
# like an environment problem at all. Leaving NRFUTIL_HOME unset lets nrfutil
# fall back to its own default resolution, which is what already works.
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr/gnu"
$env:ZEPHYR_SDK_INSTALL_DIR = "$env:NCS_TOOLCHAIN\opt\zephyr-sdk"
$env:ZEPHYR_BASE = "$env:NCS_SDK\zephyr"

# west ships as a Python module in the bundle rather than a standalone exe —
# same reasoning as ncsenv.sh's bash function, same fix.
function west { & "$env:NCS_TOOLCHAIN\opt\bin\python.exe" -m west @args }

Write-Host "ncsenv: west and python.exe on PATH from $env:NCS_TOOLCHAIN"
