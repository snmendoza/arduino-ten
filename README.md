# Dual-Mode Aurora Board

A dual-route climbing wall LED controller that simulates an [Aurora Board](https://auroraclimbing.com/) (Kilter Board, Tension Board, etc.) using Arduino R4 WiFi + ESP32 + WS2811 LED strip. Based on [1-max-1/fake_kilter_board](https://github.com/1-max-1/fake_kilter_board).

Two independent BLE endpoints allow loading two climbing routes simultaneously, displayed with distinct color schemes and animated overlap blending. An IR remote provides hands-free lane switching, visibility toggling, and horizontal mirroring.

## Hardware

| Component | Role |
|-----------|------|
| Arduino R4 WiFi | Main controller — BLE, LED strip, IR receiver |
| ESP32 WROOM | Secondary BLE peripheral — bridges a second phone/app connection to the R4 via UART |
| WS2811 LED strip (478 LEDs) | One LED per climbing hold position |
| IR receiver (pin 9) | NEC remote control input |

### Wiring
- LED data: Arduino pin 13 -> WS2811 data in
- UART bridge: ESP32 TX2 (GPIO17) -> Arduino RX1, shared GND
- IR receiver: Signal -> Arduino pin 9

## Project Structure

```
fakeAuroraBoard_arduino/       Arduino R4 main sketch (multi-file)
  fakeAuroraBoard_arduino.ino  Setup, loop, global state
  aurora_protocol.h            Protocol constants and UUIDs
  types.h                      Data types and function prototypes
  color_mapping.ino            Color decoding and route color schemes
  led_display.ino              LED rendering, animation, overlap blending
  ir_handler.ino               IR remote control processing
  ble_handler.ino              BLE connection and Aurora packet parsing
  uart_bridge.ino              UART state machine for ESP32 communication

fakeAuroraBoard_peripheral/    ESP32 BLE-to-UART bridge sketch
fakeAuroraBoard_processing/    Processing desktop visualization (upstream)
rsrc/                          Board reference images
getBoardDetails.py             Kilter Board API utility
validate_positions.py          OpenCV hold position validator
```

## Installation

1. **Arduino R4 WiFi**: Open `fakeAuroraBoard_arduino/` in Arduino IDE. Install [ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE), [FastLED](https://github.com/FastLED/FastLED), and [IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote) via **Tools > Manage Libraries**. Upload to board.

2. **ESP32 peripheral** (optional, for dual-device BLE): Open `fakeAuroraBoard_peripheral/` in Arduino IDE. Select "ESP32 Dev Module" as board. Upload. See `fakeAuroraBoard_peripheral/README.md` for wiring details.

3. Connect to the [Kilter Board app](https://kilterboardapp.com/) or [web client](https://grip-connect-kilter-board.vercel.app/). The board advertises as "Tension Board 2@3" (R4) and "Tension Board 2 - Hard Mode@3" (ESP32).

## Dual-Route Mode

Both routes are always active. The IR remote controls which lane receives incoming BLE data and visibility:

| Button | Action |
|--------|--------|
| Up Arrow (0x19) | Switch to lane 1, or toggle route 2 visibility if already there |
| Down Arrow (0x33) | Switch to lane 0, or toggle route 1 visibility if already there |
| Home (0x03) | Reset to single mode (route 1 only) |
| Return (0x78) | Mirror current lane horizontally |
| Power (0x17) | Toggle visibility of current lane |

**Route 1** uses principal colors (vivid green, pure blue, violet, scarlet).
**Route 2** uses alternative colors (lime, cyan, magenta, orange) for differentiation.
**Overlapping holds** animate with a hold-fade pattern blending both route colors.

Lane 1 auto-reverts to lane 0 after 15 seconds without new route data.

## The Aurora Board Protocol

Aurora Boards communicate over BLE, advertising service UUID `4488B571-7806-4DF6-BCFF-A2897E4953FF`. Route data is written to characteristic `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`.

### Packet format (API Level 3)

```
[START=0x01][LENGTH][CHECKSUM][TYPE_MARKER=0x02][PACKET_TYPE][HOLD_DATA...][END=0x03]
```

- **Packet types**: R (partial), S (complete), Q (query), T (test/single)
- **Hold data**: 3 bytes per hold — `[position_low][position_high][color_byte]`
- **Color encoding**: 8-bit compressed RGB (3R:3G:2B bits)
- **Checksum**: Bitwise NOT of the sum of data bytes

### UART bridge protocol (ESP32 -> R4)

```
[0xAA][0x55][count][(posL, posH, colorByte) * count][checksum]
```

Checksum is XOR of count byte and all payload bytes.

## Using Different Board Models

Board models use different position-to-coordinate mappings. The `fakeAuroraBoard_processing` sketch handles this via an SQLite database extracted from the official app. See `getBoardDetails.py` to query the Kilter Board API for gym-specific board details.

Compatible boards: Kilter Board, Tension Board, Grasshopper Board, Decoy Board, Touchstone Board.

## Changing the Board Name

Aurora board names follow the format: `Name#SerialNumber@APILevel` (serial and API level are optional). Modify `DISPLAY_NAME` and `API_LEVEL` in `aurora_protocol.h` (R4) or at the top of the ESP32 sketch.
