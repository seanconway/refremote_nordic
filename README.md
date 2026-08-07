# refremote_nordic

Firmware for a wireless officiating remote system: nRF52840-based wrist remotes worn by referees, and a USB dongle bridge that relays their input to a scoreboard application over BLE and USB.

- **Remotes** — wrist-worn devices referees use to send scoring/clock events over BLE.
- **Dongle** (`dongle/`) — nRF52840 USB bridge that receives BLE events from the remotes and forwards them to the scoreboard web app over a USB CDC-ACM serial link. Hardware: MDBT50Q-CX-40 (Nordic nRF52840 USB-C Dongle). See [`PROTOCOL.md`](PROTOCOL.md) for the wire protocol.

## Getting Started

More setup instructions will be added here as the project takes shape.

This project's current goal is to establish a reliable interface between the browser based scoreboard app and the MDBT50Q-CX-40 (Nordic nRF52840 USB-C Dongle) as detailed in PROTOCOL.md. The web app has already been updated accoridng to the interface spec and its repo can be found in the wrsl-app dir, 

Create a detailed step by step plan to implement and validate the interface. review the protocol spec, scoreboard app repo, and nordic mcp resourse for up to date documentation and best practices