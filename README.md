# refremote_nordic

Firmware for a wireless officiating remote system: nRF52840-based wrist remotes worn by referees, and a USB dongle bridge that relays their input to a scoreboard application over BLE and USB.

- **Remotes** — wrist-worn devices referees use to send scoring/clock events over BLE.
- **Dongle** (`dongle/`) — nRF52840 USB bridge that receives BLE events from the remotes and forwards them to the scoreboard web app over a USB CDC-ACM serial link. Hardware: MDBT50Q-CX-40 (Nordic nRF52840 USB-C Dongle). See [`PROTOCOL.md`](PROTOCOL.md) for the wire protocol.

## Getting Started

More setup instructions will be added here as the project takes shape.
