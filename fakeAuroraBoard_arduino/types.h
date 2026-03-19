#pragma once
#include <FastLED.h>

// Color categories for hold classification
enum HoldColor : uint8_t {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_PINK,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_BLACK,
    COLOR_UNKNOWN
};

// Structure to store hold information
struct Hold {
    uint16_t position;
    uint8_t r, g, b;
    HoldColor color;
};

// Overlap animation state (route1 vs route2)
struct OverlapInfo {
    uint16_t position;
    CRGB colorRoute1;
    CRGB colorRoute2;
};

// UART state machine states
enum UartRouteState {
    UART_WAIT_HEADER1,
    UART_WAIT_HEADER2,
    UART_WAIT_COUNT,
    UART_READ_PAYLOAD,
    UART_WAIT_CHECKSUM
};

// ---- Function prototypes for functions using custom types ----
// (Arduino auto-forward-declaration can fail for functions with custom types)

// color_mapping.ino
void decodeColor(uint8_t colorByte, uint8_t& r, uint8_t& g, uint8_t& b);
HoldColor classifyColor(uint8_t r, uint8_t g, uint8_t b);
void applyPrincipalColors(HoldColor color, uint8_t& r, uint8_t& g, uint8_t& b);
void applyAltColors(HoldColor color, uint8_t& r, uint8_t& g, uint8_t& b);

// led_display.ino
CRGB wavelengthToRGB(float wavelength);
CRGB blendColors(const CRGB& c1, const CRGB& c2, float t);
void updateOverlapState();
void updateBoardState();
void setBoardLEDs(const std::vector<Hold>& boardState);
void startupLEDs();
int mirrorPosition(int pos);
void mirrorCurrentLane();

// ir_handler.ino
void checkIRRemote();

// uart_bridge.ino
void pollESP32RouteUART();

// ble_handler.ino
void onBLEConnected(BLEDevice central);
void onBLEDisconnected(BLEDevice central);
void onDataTransferCharacteristicWritten(BLEDevice central, BLECharacteristic characteristic);
