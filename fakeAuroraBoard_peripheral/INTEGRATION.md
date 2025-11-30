# Integration Guide: Main Board + Peripheral

This document explains how to integrate the RedBear Duo peripheral with the main Arduino R4 board.

## Architecture Overview

```
┌─────────────────────┐         I2C          ┌─────────────────────┐
│   Arduino R4 WiFi   │◄─────────────────────►│   Adafruit Feather  │
│   (Main Board)      │                       │   (Peripheral)      │
│                     │                       │                     │
│ - BLE Communication│                       │ - I2C Slave         │
│ - LED Strip Control │                       │ - Route Selection   │
│ - Route Storage     │                       │ - State Management  │
│ - I2C Master        │                       │                     │
└─────────────────────┘                       └─────────────────────┘
```

## Hardware Connections

### I2C Bus Wiring

Connect the following pins between Arduino R4 and Adafruit Feather:

| Arduino R4 | Adafruit Feather | Description |
|------------|------------------|-------------|
| SDA (pin 20) | SDA | I2C Data line |
| SCL (pin 21) | SCL | I2C Clock line |
| GND | GND | Common ground |
| 5V or 3.3V | VIN/USB | Power (check Feather voltage requirements) |

**Important**: 
- Most Adafruit Feather boards have built-in I2C pull-up resistors (typically 2.2kΩ or 10kΩ)
- If you experience communication issues, you may need external 4.7kΩ pull-up resistors
- Check your specific Feather model's pinout for exact SDA/SCL pin locations

## Main Board Modifications

Add the following to your main Arduino R4 sketch:

### 1. Include Wire Library and Define I2C Address

```cpp
#include <Wire.h>

#define PERIPHERAL_I2C_ADDRESS 0x08
```

### 2. Initialize I2C in setup()

```cpp
void setup() {
    // ... existing setup code ...
    
    // Initialize I2C as master
    Wire.begin();
    
    // ... rest of setup ...
}
```

### 3. Add I2C Communication Functions

```cpp
// Send command to peripheral to enable Route 2
void peripheralEnableRoute2() {
    Wire.beginTransmission(PERIPHERAL_I2C_ADDRESS);
    Wire.write(0x01);  // Enable Route 2 command
    Wire.endTransmission();
}

// Send command to peripheral to disable Route 2
void peripheralDisableRoute2() {
    Wire.beginTransmission(PERIPHERAL_I2C_ADDRESS);
    Wire.write(0x02);  // Disable Route 2 command
    Wire.endTransmission();
}

// Query peripheral for Route 2 state
bool peripheralGetRoute2State() {
    Wire.beginTransmission(PERIPHERAL_I2C_ADDRESS);
    Wire.write(0x10);  // Request status
    Wire.endTransmission();
    
    Wire.requestFrom(PERIPHERAL_I2C_ADDRESS, 2);
    if (Wire.available() >= 2) {
        uint8_t state = Wire.read();
        uint8_t index = Wire.read();
        return (state == 0x01);
    }
    return false;
}

// Sync Route 2 state with peripheral
void syncRoute2WithPeripheral() {
    bool peripheralState = peripheralGetRoute2State();
    if (route2On != peripheralState) {
        route2On = peripheralState;
        updateBoardState();
        Serial.print("Route 2 synced with peripheral: ");
        Serial.println(route2On ? "ON" : "OFF");
    }
}
```

### 4. Update Route Assignment Logic

Modify the route assignment section in `onDataTransferCharacteristicWritten()` to check peripheral state:

```cpp
// In the route completion handling section (around line 582)
if (packetTypeChar == 'S' || packetTypeChar == 'T') {
    // Check if peripheral wants this route on secondary channel
    bool useSecondaryChannel = peripheralGetRoute2State();
    
    if (useSecondaryChannel && currentLane == 1) {
        // Store in Route 2 with alternative colors
        route2Holds.clear();
        route2Holds = tempHolds;
        for (Hold& h : route2Holds) {
            applyAltColors(h.colorName, h.r, h.g, h.b);
        }
        Serial.println("\nRoute 2 stored (alternative colors) - from peripheral selection");
    } else if (currentLane == 0) {
        // Store in Route 1 with principal colors
        route1Holds.clear();
        route1Holds = tempHolds;
        for (Hold& h : route1Holds) {
            applyPrincipalColors(h.colorName, h.r, h.g, h.b);
        }
        Serial.println("\nRoute 1 stored (principal colors)");
    }
    
    // ... rest of existing code ...
}
```

### 5. Periodic Sync in loop()

Add periodic synchronization with peripheral:

```cpp
void loop() {
    // ... existing loop code ...
    
    // Sync with peripheral every 500ms
    static unsigned long lastPeripheralSync = 0;
    if (millis() - lastPeripheralSync > 500) {
        syncRoute2WithPeripheral();
        lastPeripheralSync = millis();
    }
    
    // ... rest of loop ...
}
```

## Usage Flow

1. **Initialization**: Both boards power on and initialize I2C
2. **Route Selection**: User selects route via peripheral (or peripheral receives command)
3. **Route Reception**: Main board receives route data via BLE
4. **Channel Assignment**: Main board checks peripheral state to determine which channel to use
5. **Display**: Route appears on appropriate channel (Route 1 or Route 2) based on peripheral selection

## Testing

1. **Verify I2C Connection**:
   - Upload peripheral sketch to Adafruit Feather (select correct board in Arduino IDE)
   - Upload modified main board sketch
   - Check Serial output on both devices (115200 baud)
   - Peripheral should show "Adafruit Feather Peripheral Controller" and "Ready to receive commands"
   - Main board should successfully query peripheral

2. **Test Commands**:
   - Send enable/disable commands from main board
   - Verify peripheral responds correctly
   - Check that Route 2 visibility updates accordingly

3. **Test Route Assignment**:
   - Connect via BLE and send a route
   - Verify route appears on correct channel based on peripheral state

## Troubleshooting

- **No I2C Communication**: Check wiring, pull-up resistors, and I2C addresses
- **Incorrect State**: Verify peripheral is responding to status requests
- **Route Not Appearing**: Check that route assignment logic respects peripheral state
- **Serial Debugging**: Enable Serial output on both devices to trace communication

## Notes

- I2C communication is relatively slow, so don't query peripheral too frequently
- Peripheral maintains its own state independently
- Main board can override peripheral state if needed (for IR remote, etc.)
- Consider adding error handling for I2C communication failures

