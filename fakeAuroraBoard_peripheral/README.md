# ESP32-S BLE-to-I2C Bridge

This is the BLE-to-I2C bridge peripheral for the Aurora Board system, designed to run on an **ESP32-S** microcontroller.

## Purpose

The peripheral controller receives route data from the Tension Board app via BLE and forwards it to the main Arduino R4 board over I2C. This allows users to choose which BLE device to connect to (R4's built-in BLE or ESP32-S BLE).

## Hardware Requirements

- **ESP32-S** (Arduino-compatible, built-in BLE)
- **I2C Connection** to main Arduino R4 board
  - SDA: Connect to main board's SDA pin (pin 20 on R4)
  - SCL: Connect to main board's SCL pin (pin 21 on R4)
  - Pull-up resistors: 4.7kΩ on both SDA and SCL (if not built-in)

## Important: Library Conflict Resolution

**CRITICAL**: The ArduinoBLE library conflicts with ESP32's native BLE library. You must either:

1. **Disable ArduinoBLE for ESP32** (Recommended):
   - In Arduino IDE, go to `File > Preferences`
   - Find the sketchbook location
   - Navigate to `libraries/ArduinoBLE`
   - Temporarily rename the folder (e.g., `ArduinoBLE.disabled`) when compiling for ESP32

2. **Or ensure ESP32 board is selected**:
   - Make sure `Tools > Board` shows an ESP32 board (not Arduino board)
   - ESP32 board package includes its own BLE library that will be used

## Installation

1. **Install ESP32 Board Support**:
   - In Arduino IDE, go to `File > Preferences`
   - Add this URL to "Additional Boards Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to `Tools > Board > Boards Manager`
   - Search for "ESP32" and install "esp32 by Espressif Systems"

2. **Select Board**:
   - Go to `Tools > Board`
   - Select your ESP32 board (e.g., "ESP32 Dev Module" or your specific ESP32-S variant)

3. **Upload Sketch**: Upload this sketch to your ESP32-S

## Wiring

### I2C to Arduino R4:
- ESP32 SDA → Arduino R4 SDA (pin 20)
- ESP32 SCL → Arduino R4 SCL (pin 21)
- ESP32 GND → Arduino R4 GND

### Power:
- ESP32 can be powered from R4's 3.3V or 5V pin (check your ESP32-S voltage requirements)
- Or power both from 12V source separately

## I2C Protocol

The peripheral sends packets to the R4 in this format:
- **2 bytes**: Packet length (high byte, low byte)
- **N bytes**: Complete Aurora Board protocol packet

## Usage

1. Upload the sketch to ESP32-S
2. Wire I2C connections between ESP32-S and Arduino R4
3. Power on both devices
4. ESP32-S will advertise as "Tension Board 2 - Hard" via BLE
5. Users can connect to either:
   - R4's built-in BLE (e.g., "Tension Board 2")
   - ESP32-S BLE (e.g., "Tension Board 2 - Hard")
6. Route data received on ESP32-S will be forwarded to R4 over I2C

## Troubleshooting

- **Library conflicts**: Make sure ArduinoBLE is disabled when compiling for ESP32
- **I2C communication**: Check wiring, pull-up resistors, and I2C addresses
- **BLE not advertising**: Check Serial output for initialization errors
- **No data forwarding**: Verify I2C connection and R4 is listening on address 0x08

## Notes

- ESP32-S uses native BLE support (not ArduinoBLE library)
- Code uses ESP32's BLEDevice, BLEServer, BLECharacteristic classes
- I2C address is 0x08 (matches R4's expected address)
- Communication is one-way: ESP32-S → R4 (ESP32-S receives BLE, sends I2C)
